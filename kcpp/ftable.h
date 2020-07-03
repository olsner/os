#pragma once

#include <vector>
#include "refcnt.h"

struct Socket;

struct File: RefCounted<File> {
    Process* owner = nullptr;

    virtual ~File() {}

    // TODO virtual calls for any fd-related syscalls? then Socket can implement most of the IPC.

    // Socket is the only kind of file we have for now.
    RefCnt<Socket> get_socket();
};

namespace aspace {

// A table for storing file descriptors -> file mappings
struct FTable {
    std::vector<RefCnt<File>> files;
    //Mutex mutex;

    int get_num_files() const {
        return files.size();
    }

    RefCnt<File> get_file(int fd) const {
        //unique_lock l(mutex);
        if (size_t(fd) < files.size()) {
            return files[fd];
        }
        else {
            return nullptr;
        }
    }

    int add_file(RefCnt<File> f) {
        auto it = std::find(files.begin(), files.end(), nullptr);
        if (it == files.end()) {
            int fd = (int)files.size();
            files.push_back(std::move(f));
            log(files, "Added file %d, ftable size now %zu\n", fd, files.size());
            return fd;
        }
        log(files, "Found unused file slot at %zu\n", it - files.begin());
        *it = std::move(f);
        return it - files.begin();
    }

    // Insert a file at a specific place in the fd table, replacing whatever
    // was there.
    RefCnt<File> replace_file(int fd, RefCnt<File> new_file) {
        if (files.size() <= size_t(fd)) {
            files.resize(fd + 1, nullptr);
        }
        auto old_file = latch(files[fd], std::move(new_file));
        // TODO Shrink table if we inserted a nullptr in the last entry.
        return old_file;
    }

    int get_file_number(const RefCnt<File>& file) const {
        auto it = std::find(files.begin(), files.end(), file);
        if (it == files.end()) {
            return -1;
        }
        return it - files.begin();
    }
};

}
