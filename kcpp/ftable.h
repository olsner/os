#pragma once

#include <vector>
#include "refcnt.h"

struct File: RefCounted<File> {
    // Until we come up with a better API, record an owner of each file.
    // Open-ended receives are received only by the owner.
    Process* owner = nullptr;

    virtual ~File() {}

    // TODO virtual calls for any fd-related syscalls? then Socket can implement most of the IPC.

    // Socket is the only kind of file we have for now.
    RefCnt<Socket> get_socket() { return RefCnt(static_cast<Socket*>(this)); }
};

struct Transaction {
    // Process that sent the original message that the response should be sent
    // to. The process is guaranteed to be blocked in a sendrcv call, since
    // that's the only thing that starts a transaction.
    // TODO Use a smart/weak pointer.
    Process* peer = nullptr;
    // MSG_TX_* flags and transaction index for matching.
    // Could be stored shifted down by some amount to e.g. fit in uint8_t.
    uint64_t tx_id = 0;

    Transaction() = default;
    Transaction(Process* peer, uint64_t tx_id): peer(peer), tx_id(tx_id) {}

    // Override so that we reset tx_id to 0 when moving from a transaction.
    Transaction(Transaction&& other): peer(std::exchange(peer, nullptr)), tx_id(std::exchange(other.tx_id, 0)) {}
    Transaction& operator=(Transaction&& other) {
        peer = std::exchange(peer, nullptr);
        tx_id = std::exchange(other.tx_id, 0);
        return *this;
    }

    bool used() const { return peer != nullptr; }
    bool matches(uint64_t tx_id) const { return this->tx_id == tx_id; }
};

struct Socket: File {
    static constexpr size_t NTX = 8;
    Transaction tx_table[NTX];
    Socket* other_side = nullptr; // TODO weak_ptr
    bool server_side;

    Socket(bool server): server_side(server) {}
    ~Socket() {
        for (Transaction& t: tx_table) {
            assert(!t.used());
        }
    }

    static void pair(RefCnt<Socket>& server, RefCnt<Socket>& client) {
        server = make_refcnt<Socket>(true);
        client = make_refcnt<Socket>(false);
        server->other_side = client.get();
        client->other_side = server.get();
    }

    // Allocate a transaction and return its tx_id.
    uint64_t start_transaction(Process* waiter, uint64_t flags) {
        for (size_t i = 0; i < NTX; i++) {
            if (!tx_table[i].used()) {
                uint64_t tx_id = (uint64_t(i + 1) << 32) | flags;
                tx_table[i] = Transaction(waiter, tx_id);
                return tx_id;
            }
        }
        return 0;
    }

    Transaction end_transaction(uint64_t tx_id) {
        // Extract the high bits (tx + flag) to get the index. tx_id is off by
        // one to reserve tx value 0, then mask out the relevant bits.
        size_t index = ((tx_id >> 32) - 1) & (NTX - 1);
        // No range check necessary since we mask down to NTX - 1.
        if (tx_table[index].matches(tx_id)) {
            return std::move(tx_table[index]);
        }
        // Indicate failure by returning an empty transaction
        return Transaction();
    }
};

namespace aspace {

// A table for storing file descriptors -> file mappings
struct FTable {
    std::vector<RefCnt<File>> files;
    //Mutex mutex;

    RefCnt<File> get_file(int fd) {
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
    // TODO Return a smart pointer to the old file - maybe this can be useful?
    // It'll also ensure the old file gets released.
    void replace_file(int fd, RefCnt<File> new_file) {
        if (files.size() <= size_t(fd)) {
            files.resize(fd + 1, nullptr);
        }
        files[fd] = std::move(new_file);
        // TODO Shrink table if we inserted a nullptr in the last entry.
    }
};

}
