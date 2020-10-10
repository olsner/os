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

template <> const char* nameof<File> = "File";

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

    // Resize the file table if necessary and return a reference to the given
    // file table entry.
    RefCnt<File>& at(int fd) {
        if (files.size() <= size_t(fd)) {
            files.resize(fd + 1, nullptr);
        }
        return files[fd];
    }

    int get_file_number(const RefCnt<File>& file) const {
        return get_file_number(file.get());
    }
    int get_file_number(File* file) const {
        auto it = std::find(files.begin(), files.end(), file);
        if (it == files.end()) {
            return -1;
        }
        return it - files.begin();
    }
};

}
