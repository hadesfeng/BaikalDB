// Copyright (c) 2018 Baidu, Inc. All Rights Reserved.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "raft_snapshot_adaptor.h"
#include "mut_table_key.h"
#include "sst_file_writer.h"
#include "meta_writer.h"
#include "log_entry_reader.h"

namespace baikaldb {

bool inline is_snapshot_data_file(const std::string& path) {
    butil::StringPiece sp(path);
    if (sp.ends_with(SNAPSHOT_DATA_FILE_WITH_SLASH)) {
        return true;
    }
    return false;
}
bool inline is_snapshot_meta_file(const std::string& path) {
    butil::StringPiece sp(path);
    if (sp.ends_with(SNAPSHOT_META_FILE_WITH_SLASH)) {
        return true;
    }
    return false;
}
bool PosixDirReader::is_valid() const {
    return _dir_reader.IsValid();
}

bool PosixDirReader::next() {
    bool rc = _dir_reader.Next();
    while (rc && (strcmp(name(), ".") == 0 || strcmp(name(), "..") == 0)) {
        rc = _dir_reader.Next();
    }
    return rc;
}

const char* PosixDirReader::name() const {
    return _dir_reader.name();
}

RocksdbSnapshotReader::RocksdbSnapshotReader(int64_t region_id,
                                            const std::string& path,
                                            RaftSnapshotAdaptor* rs,
                                            SnapshotContextPtr context,
                                            bool is_meta_reader) :
            _region_id(region_id), 
            _path(path),
            _rs(rs),
            _context(context),
            _is_meta_reader(is_meta_reader),
            _closed(true)   {}

RocksdbSnapshotReader::~RocksdbSnapshotReader() {
    close();
}

ssize_t RocksdbSnapshotReader::read(butil::IOPortal* portal, off_t offset, size_t size) {
    if (_closed) {
        DB_FATAL("rocksdb reader has been closed, region_id: %ls, offset: %ld",
                    _region_id, offset);
        return -1;
    }
    if (offset < 0) {
        DB_FATAL("region_id: %ld red error. offset: %ld", _region_id, offset);
        return -1;
    }

    TimeCost time_cost;
    //DB_WARNING("region_id: %ld req:[offset: %ld, size: %ld], local:[offset: %ld]",
    //            _region_id, offset, size, iter_context->offset);
    IteratorContext* iter_context = _context->data_context;
    if (_is_meta_reader) {
        iter_context = _context->meta_context;
    } 
    if (offset < iter_context->offset) {
        iter_context->offset = 0;
        iter_context->iter->Seek(iter_context->prefix);
    }

    size_t count = 0;
    int64_t key_num = 0;
    std::string log_index_prefix = MetaWriter::get_instance()->log_index_key_prefix(_region_id);
    std::string txn_info_prefix = MetaWriter::get_instance()->transcation_pb_key_prefix(_region_id);
    while (count < size) {
        if (!iter_context->iter->Valid()
                || !iter_context->iter->key().starts_with(iter_context->prefix)) {
            iter_context->done = true;
            DB_WARNING("region_id: %ld snapshot read over, total size: %ld", _region_id, iter_context->offset);
            break;
        }
        //txn_info请求不发送，理论上leader上没有该类请求
        if (iter_context->is_meta_sst && iter_context->iter->key().starts_with(txn_info_prefix)) {
            iter_context->iter->Next();
            continue;
        }
        //如果是prepared事务的log_index记录需要解析出store_req
        int64_t read_size = 0;
        if (iter_context->is_meta_sst && iter_context->iter->key().starts_with(log_index_prefix)) {
            ++key_num;
            int64_t log_index = MetaWriter::get_instance()->decode_log_index_value(iter_context->iter->value());
            std::string txn_info;
            auto ret  = LogEntryReader::get_instance()->read_log_entry(_region_id, log_index, txn_info);
            if (ret < 0) {
                iter_context->done = true;
                DB_FATAL("read txn info fail, may be has removed, region_id: %ld", _region_id);
                return -1;
            }
            if (iter_context->offset >= offset) {
                read_size += append_to_iobuf(portal, MetaWriter::get_instance()->transcation_pb_key(_region_id, log_index));
                read_size += append_to_iobuf(portal, rocksdb::Slice(txn_info));
            } else {
                read_size += append_to_iobuf(nullptr, MetaWriter::get_instance()->transcation_pb_key(_region_id, log_index));
                read_size += append_to_iobuf(nullptr, rocksdb::Slice(txn_info));
            }
        } else {
            key_num++;
            if (iter_context->offset >= offset) {
                read_size += append_to_iobuf(portal, iter_context->iter->key());
                read_size += append_to_iobuf(portal, iter_context->iter->value());
            } else {
                read_size += append_to_iobuf(nullptr, iter_context->iter->key());
                read_size += append_to_iobuf(nullptr, iter_context->iter->value());
            } 
        }
        count += read_size;
        iter_context->offset += read_size;
        iter_context->iter->Next();
    }
    DB_WARNING("region_id: %ld read done. count: %ld, key_num: %ld, time_cost: %ld", 
                _region_id, count, key_num, time_cost.get_time());
    return count;
}

// Method 'destroy' will be called after 'FileSystemAdaptor::open' and 'read' every time in raft.
// In performance and cost considerations, we do not really destroy the Reader
// when data do not be read done. We will only destroy the Reader when data has been read done
// or some error occurred
bool RocksdbSnapshotReader::close() {
    if (_closed) {
        DB_WARNING("file has been closed, path: %s", _path.c_str());
        return true;
    }
    _rs->close(_path);
    _closed = true;
    return true;
}

ssize_t RocksdbSnapshotReader::size() {
    IteratorContext* iter_context = _context->data_context;
    if (_is_meta_reader) {
        iter_context = _context->meta_context;
    } 
    
    if (iter_context->done) {
        return iter_context->offset;
    }
    return std::numeric_limits<ssize_t>::max();
}

ssize_t RocksdbSnapshotReader::write(const butil::IOBuf& data, off_t offset) {
    (void)data;
    (void)offset;
    DB_FATAL("RocksdbSnapshotReader::write not implemented");
    return -1;
}

bool RocksdbSnapshotReader::sync() {
    DB_FATAL("RocksdbSnapshotReader::sync not implemented");
    return false;
}

RocksdbSstWriter::RocksdbSstWriter(int64_t region_id, const std::string& path, const rocksdb::Options& option)
        : _region_id(region_id)
        , _closed(true)
        , _path(path)
        , _count(0)
        , _writer(new SstFileWriter(option)) {}

int RocksdbSstWriter::open() {
    auto s = _writer->open(_path);
    if (!s.ok()) {
        DB_FATAL("open sst file path: %s failed, err: %s, region_id: %ld", _path.c_str(), s.ToString().c_str());
        return -1;
    }
    _closed = false;
    DB_WARNING("rocksdb sst writer open, path: %s, region_id: %ld", _path.c_str(), _region_id);
    return 0;
}

ssize_t RocksdbSstWriter::write(const butil::IOBuf& data, off_t offset) {
    (void)offset;
    if (_closed) {
        return -1;
    }
    //DB_WARNING("rocksdb sst write, offset: %lu, data.size: %ld", offset, data.size());
    std::vector<std::string> keys;
    std::vector<std::string> values;
    auto ret = parse_from_iobuf(data, keys, values);
    if (ret < 0) {
        DB_FATAL("write sst file path: %s failed, received invalid data, data len: %d, region_id: %ld",
                _path.c_str(), data.size(), _region_id);
        return -1;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        auto s = _writer->put(rocksdb::Slice(keys[i]), rocksdb::Slice(values[i]));
        if (!s.ok()) {            
            DB_FATAL("write sst file path: %s failed, err: %s, region_id: %ld", 
                        _path.c_str(), s.ToString().c_str(), _region_id);
            return -1;
        }
        _count++;
    }
    DB_WARNING("rocksdb sst write, region_id: %ld, path: %s, offset: %lu, data.size: %ld,"
                " keys size: %ld, total_count: %ld", _region_id, _path.c_str(), offset, data.size(),
                    keys.size(), _count);
    return data.size();
}

bool RocksdbSstWriter::close() {
    if (_closed) {
        DB_WARNING("file has been closed, path: %s", _path.c_str());
        return true;
    }
    _closed = true;
    bool ret = true;
    if (_count > 0) {
        auto s = _writer->finish();
        DB_WARNING("_writer finished, path: %s, region_id: %ld", _path.c_str(), _region_id);
        if (!s.ok()) {
            DB_FATAL("finish sst file path: %s failed, err: %s, region_id: %ld", 
                    _path.c_str(), s.ToString().c_str(), _region_id);
            ret = false;
        }
    } else {
        ret = butil::DeleteFile(butil::FilePath(_path), false);
        DB_WARNING("count is 0, delete path: %s, region_id: %ld", _path.c_str(), _region_id);
        if (!ret) {
            DB_FATAL("delete sst file path: %s failed, region_id: %ld", 
                        _path.c_str(), _region_id);
        }
    }
    return ret;
}

RocksdbSstWriter::~RocksdbSstWriter() {
    close();
}

ssize_t RocksdbSstWriter::size() {
    DB_FATAL("RocksdbSstWriter::size not implemented, region_id: %ld", _region_id);
    return -1;
}

bool RocksdbSstWriter::sync() {
    DB_FATAL("RocksdbSstWriter::sync not implemented, region_id: %ld", _region_id);
    return false;
}

ssize_t RocksdbSstWriter::read(butil::IOPortal* portal, off_t offset, size_t size) {
    DB_FATAL("RocksdbSstWriter::read not implemented, region_id: %ld", _region_id);
    return -1;
}

PosixFileAdaptor::~PosixFileAdaptor() {
    close();
}

int PosixFileAdaptor::open(int oflag) {
    oflag &= (~O_CLOEXEC);
    _fd = ::open(_path.c_str(), oflag, 0644);
    if (_fd <= 0) {
        return -1;
    }
    return 0;
}

bool PosixFileAdaptor::close() {
    if (_fd > 0) {
        bool res = ::close(_fd) == 0;
        _fd = -1;
        return res;
    }
    return true;
}

ssize_t PosixFileAdaptor::write(const butil::IOBuf& data, off_t offset) {
    ssize_t ret = braft::file_pwrite(data, _fd, offset);
    return ret;
}

ssize_t PosixFileAdaptor::read(butil::IOPortal* portal, off_t offset, size_t size) {
    return braft::file_pread(portal, _fd, offset, size);
}

ssize_t PosixFileAdaptor::size() {
    off_t sz = lseek(_fd, 0, SEEK_END);
    return ssize_t(sz);
}

bool PosixFileAdaptor::sync() {
    return braft::raft_fsync(_fd) == 0;
}

RaftSnapshotAdaptor::RaftSnapshotAdaptor(int64_t region_id) : _region_id(region_id) {}

RaftSnapshotAdaptor::~RaftSnapshotAdaptor() {
    // Before destroy adaptor, we need to wait all reading snapshot done
    // to avoid resource released while in use
    std::unique_lock<std::mutex> lock(_snapshot_mutex);
    _empty_cond.wait(lock, [this] {
            return _snapshots.empty();
            });
    DB_WARNING("region_id: %ld RaftSnapshotAdaptor released", _region_id);
}

braft::FileAdaptor* RaftSnapshotAdaptor::open(const std::string& path, int oflag,
                                     const ::google::protobuf::Message* file_meta,
                                     butil::File::Error* e) {
    //regular file
    if (!is_snapshot_data_file(path) && !is_snapshot_meta_file(path)) {
        PosixFileAdaptor* adaptor = new PosixFileAdaptor(path);
        int ret = adaptor->open(oflag);
        if (ret != 0) {
            if (e) {
                *e = butil::File::OSErrorToFileError(errno);
            }
            delete adaptor;
            return nullptr;
        }
        DB_WARNING("open file: %s, region_id: %ld", path.c_str(), _region_id);
        return adaptor;
    }

    bool for_write = (O_WRONLY & oflag);
    if (for_write) {
        return open_for_write(path, oflag, file_meta, e);
    }
    return open_for_read(path, oflag, file_meta, e);
}

braft::FileAdaptor* RaftSnapshotAdaptor::open_for_write(const std::string& path, int oflag,
                                     const ::google::protobuf::Message* file_meta,
                                     butil::File::Error* e) {
    (void) file_meta;

    RocksWrapper* db = RocksWrapper::get_instance();
    rocksdb::Options options;
    if (is_snapshot_data_file(path)) {
        options = db->get_options(db->get_data_handle()); 
    } else {
        options = db->get_options(db->get_meta_info_handle());
    }
    
    RocksdbSstWriter* writer = new RocksdbSstWriter(_region_id, path, options);
    int ret = writer->open();
    if (ret != 0) {
        if (e) {
            *e = butil::File::FILE_ERROR_FAILED;
        }
        delete writer;
        return nullptr;
    }
    DB_WARNING("open for write file, path: %s, region_id: %ld", path.c_str(), _region_id);
    return writer;
}

braft::FileAdaptor* RaftSnapshotAdaptor::open_for_read(const std::string& path, int oflag,
                                     const ::google::protobuf::Message* file_meta,
                                     butil::File::Error* e) {
    TimeCost time_cost;
    (void) file_meta;
    std::string prefix;
    size_t len = path.size();
    if (is_snapshot_data_file(path)) {
        len -= SNAPSHOT_DATA_FILE.size();
        MutTableKey prefix_key;
        prefix_key.append_i64(_region_id);
        prefix = prefix_key.data();
    } else {
        len -= SNAPSHOT_META_FILE.size();
        prefix = MetaWriter::get_instance()->meta_info_prefix(_region_id);
    }
    const std::string snapshot_path = path.substr(0, len - 1);
    std::unique_lock<std::mutex> lock(_snapshot_mutex);
    auto sc = get_snapshot(snapshot_path);
    if (sc == nullptr) {
        lock.unlock();
        DB_FATAL("snapshot no found, path: %s, region_id: %ld", snapshot_path.c_str(), _region_id);
        if (e != nullptr) {
            *e = butil::File::FILE_ERROR_NOT_FOUND;
        }
        return nullptr;
    }
    bool is_meta_reader = false;
    IteratorContext* iter_context = nullptr;
    rocksdb::ReadOptions read_options;
    read_options.snapshot = sc->snapshot;
    if (is_snapshot_data_file(path)) {
        is_meta_reader = false;
        iter_context = sc->data_context;
        //first open snapshot file
        if (iter_context == nullptr) {
            iter_context = new IteratorContext;
            iter_context->prefix = prefix;
            iter_context->is_meta_sst = false;
            read_options.total_order_seek = true;
            rocksdb::ColumnFamilyHandle* column_family = RocksWrapper::get_instance()->get_data_handle();
            iter_context->iter.reset(RocksWrapper::get_instance()->new_iterator(read_options, column_family));
            iter_context->iter->Seek(prefix);
            sc->data_context = iter_context;
        }
    }
    if (is_snapshot_meta_file(path)) {
        is_meta_reader = true;
        iter_context = sc->meta_context;
        if (iter_context == nullptr) {
            iter_context = new IteratorContext;
            iter_context->prefix = prefix;
            iter_context->is_meta_sst = true;
            read_options.prefix_same_as_start = true;
            read_options.total_order_seek = false;
            rocksdb::ColumnFamilyHandle* column_family = RocksWrapper::get_instance()->get_meta_info_handle();
            iter_context->iter.reset(RocksWrapper::get_instance()->new_iterator(read_options, column_family));
            iter_context->iter->Seek(prefix);
            sc->meta_context = iter_context;
        }
    }
    if (iter_context->reading) {
        DB_WARNING("snapshot reader is busy, path: %s, region_id: %ld", path.c_str(), _region_id);
        if (e != nullptr) {
            *e = butil::File::FILE_ERROR_IN_USE;
        }
        return nullptr;
    }
    iter_context->reading = true;
    auto reader = new RocksdbSnapshotReader(_region_id, path, this, sc, is_meta_reader);
    reader->open();
    DB_WARNING("region_id: %ld open reader: path: %s, time_cost: %ld", 
                _region_id, path.c_str(), time_cost.get_time());
    return reader;
}

bool RaftSnapshotAdaptor::delete_file(const std::string& path, bool recursive) {
    butil::FilePath file_path(path);
    return butil::DeleteFile(file_path, recursive);
}

bool RaftSnapshotAdaptor::rename(const std::string& old_path, const std::string& new_path) {
    return ::rename(old_path.c_str(), new_path.c_str()) == 0;
}

bool RaftSnapshotAdaptor::link(const std::string& old_path, const std::string& new_path) {
    return ::link(old_path.c_str(), new_path.c_str()) == 0;
}

bool RaftSnapshotAdaptor::create_directory(const std::string& path,
                                         butil::File::Error* error,
                                         bool create_parent_directories) {
    butil::FilePath dir(path);
    return butil::CreateDirectoryAndGetError(dir, error, create_parent_directories);
}

bool RaftSnapshotAdaptor::path_exists(const std::string& path) {
    butil::FilePath file_path(path);
    return butil::PathExists(file_path);
}

bool RaftSnapshotAdaptor::directory_exists(const std::string& path) {
    butil::FilePath file_path(path);
    return butil::DirectoryExists(file_path);
}

braft::DirReader* RaftSnapshotAdaptor::directory_reader(const std::string& path) {
    return new PosixDirReader(path.c_str());
}

bool RaftSnapshotAdaptor::open_snapshot(const std::string& path) {
    _snapshot_mutex.lock();
    auto iter = _snapshots.find(path);
    if (iter != _snapshots.end()) {
        _snapshot_mutex.unlock();
        // We do not allow multi peer to read the same snapshot concurrently
        // But allow them to read different snapshot concurrently
        DB_WARNING("region_id: %ld snapshot path: %s is busy", _region_id, path.c_str());
        _snapshots[path].second++;
        return false;
    }

    // create new Rocksdb Snapshot
    _snapshots[path].first.reset(new SnapshotContext());
    _snapshots[path].second++;
    _snapshot_mutex.unlock();
    DB_WARNING("region_id: %ld open snapshot path: %s", _region_id, path.c_str());
    return true;
}

void RaftSnapshotAdaptor::close_snapshot(const std::string& path) {
    DB_WARNING("region_id: %ld close snapshot path: %s", _region_id, path.c_str());
    std::lock_guard<std::mutex> guard(_snapshot_mutex);
    auto iter = _snapshots.find(path);
    if (iter != _snapshots.end()) {
        _snapshots[path].second--;
        if (_snapshots[path].second == 0) {
            _snapshots.erase(iter);
            DB_WARNING("region_id: %ld close snapshot path: %s relase", _region_id, path.c_str());
        }
    }
    // Notify someone waiting to destroy '_snapshots'
    _empty_cond.notify_all();
}

SnapshotContextPtr RaftSnapshotAdaptor::get_snapshot(const std::string& path) {
    auto iter = _snapshots.find(path);
    if (iter != _snapshots.end()) {
        return iter->second.first;
    }
    return nullptr;
}

void RaftSnapshotAdaptor::close(const std::string& path) {
    size_t len = path.size();
    if (is_snapshot_data_file(path)) {
        len -= SNAPSHOT_DATA_FILE_WITH_SLASH.size();
    } else {
        len -= SNAPSHOT_META_FILE_WITH_SLASH.size();
    }
    const std::string snapshot_path = path.substr(0, len);

    std::lock_guard<std::mutex> lock(_snapshot_mutex);
    auto iter = _snapshots.find(snapshot_path);
    if (iter == _snapshots.end()) {
        DB_FATAL("no snapshot found when close reader, path:%s, region_id: %ld", 
                    path.c_str(), _region_id);
        return;
    }
    auto& snapshot_ctx = iter->second;
    if (is_snapshot_data_file(path)) {
        DB_WARNING("read snapshot data file close, path: %s", path.c_str());
        snapshot_ctx.first->data_context->reading = false;
    } else {
        DB_WARNING("read snapshot meta data file close, path: %s", path.c_str());
        snapshot_ctx.first->meta_context->reading = false;
    }
}

}//namespace

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
