#pragma once

#include "refcnt.h"
#include "ftable.h"

struct Transaction {
    // Process that sent the original message that the response should be sent
    // to. The process is guaranteed to be blocked in a sendrcv call, since
    // that's the only thing that starts a transaction.
    // TODO Use a smart/weak pointer.
    Process* peer = nullptr;
    // MSG_TX_* flags and transaction index for matching.
    // Could be stored shifted down by some amount to e.g. fit in uint8_t.
    uint64_t id = 0;

    Transaction() = default;
    Transaction(Process* peer, uint64_t id): peer(peer), id(id) {}

    // Need to reset id to 0 when moving from a transaction,
    // even if 'peer' is made a smart pointer that moves itself.
    Transaction(Transaction&& other):
        peer(std::exchange(other.peer, nullptr)),
        id(std::exchange(other.id, 0))
    {}
    Transaction& operator=(Transaction&& other) {
        peer = std::exchange(other.peer, nullptr);
        id = std::exchange(other.id, 0);
        return *this;
    }

    bool used() const { return peer != nullptr; }
    bool matches(uint64_t id) const { return this->id == id; }
    operator bool() const { return used(); }
};

class Socket final : public File {
    static constexpr size_t NTX = 8;
    // Mask for flags that must match in transaction IDs. Removes the file
    // descriptor (as that's different for each process involved) and flags
    // that don't need to match.
    static constexpr uint64_t tx_mask = msg_set_fd(~MSG_TX_CLOSEFD, 0);

    Transaction tx_table[NTX];
private:
    // Some kind of manual weak_ptr, replace it with an actual weak_ptr once
    // we have those.
    Socket* other_side = nullptr;
    // List of processes sending or receiving on this end of the socket. This
    // does not cover processes currently in a transaction.
    DList<Process> waiters;
    uintptr_t events = 0;

public:
    // server_side isn't recorded since it doesn't actually matter. This is
    // nice because "server" and "client" is more a matter of which process
    // is doing the communication than how the socket fd was created.
    Socket(bool server_side UNUSED) {}
    ~Socket() {
        for (Transaction& tx: tx_table) {
            assert(!tx.used());
        }

        // TODO Iterate receivers, any that receive specifically from this file should now get an error.
        // (e.g. one thread does close() while another receives)
        // This implies the process -> file link should be weak.

        if (other_side) {
            other_side->other_side = nullptr;
        }
    }

    static void pair(RefCnt<Socket>& server, RefCnt<Socket>& client) {
        server = make_refcnt<Socket>(true);
        client = make_refcnt<Socket>(false);
        server->other_side = client.get();
        client->other_side = server.get();
    }

    RefCnt<Socket> get_other() const {
        // Eventually other_side might be a weak_ptr that has official ways
        // to convert to a strong shared_ptr.
        return RefCnt<Socket>::from_raw(other_side);
    }

    // Allocate a transaction and return its id.
    uint64_t start_transaction(Process* waiter, uint64_t fd_flags) {
        const u64 flags = fd_flags & tx_mask;
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
        auto& tx = tx_table[index];

        // This checks that the flags match - e.g. TX_ACCEPTFD transaction must
        // be responded with TX_ACCEPTFD / TX_FD.
        if (tx.matches(tx_id & tx_mask)) {
            return std::move(tx);
        }
        // Indicate failure by returning an empty transaction
        return Transaction();
    }

    Transaction get_transaction() {
        for (Transaction& tx : tx_table) {
            if (tx.used()) return std::move(tx);
        }
        return Transaction();
    }

    // Get a process willing to receive a non-transaction message.
    Process* get_recipient() {
        for (auto p : waiters) {
            if (p->can_receive_from(this)) {
                return waiters.remove(p);
            }
        }
        if (owner && owner->can_receive_from(this)) {
            return owner;
        }
        return nullptr;
    }
    Process* get_sender() {
        for (auto p : waiters) {
            if (p->sending_to(this)) {
                return waiters.remove(p);
            }
        }
        if (owner && owner->sending_to(this)) {
            return owner;
        }
        return nullptr;
    }
    void add_waiter(Process* p) {
        assert(p->blocked_socket.get() == this);
        waiters.append(p);
    }

    void add_event_bits(uintptr_t bits) {
        events |= bits;
    }
    uintptr_t get_reset_event_bits() {
        return latch(events);
    }
};

template <> constexpr const char* nameof<Socket> = "Socket";

void Process::block_on_socket(const RefCnt<Socket>& sock) {
    assert(!blocked_socket);
    assert(!is_runnable());
    blocked_socket = sock;
    sock->add_waiter(this);
}
