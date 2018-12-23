#include <memory>

template <class T> struct RefList;

template <class T> class RefListIterator
{
    using Ptr = std::shared_ptr<T>;
    using Node = std::__shared_ptr_impl<T>;
    Node *node;

    friend class RefList<T>;

    RefListIterator(Node *node): node(node) {
    }

    static RefListIterator end() {
        return RefListIterator((Node*)nullptr);
    }

public:
    RefListIterator operator ++(int) {
        RefListIterator res = *this;
        ++*this;
        return res;
    }
    RefListIterator& operator ++() {
        assert(node);
        node = node->next ? node->next : nullptr;
        return *this;
    }
    bool operator==(const RefListIterator &other) const {
        return node == other.node;
    }
    bool operator!=(const RefListIterator &other) const {
        return node != other.node;
    }
    Ptr operator *() const {
        // This adds a reference since the object may be used outside the list
        // once it's been returned.
        return Ptr(node);
    }
};
template <class T> struct RefList
{
    using Ptr = std::shared_ptr<T>;
    using Node = std::__shared_ptr_impl<T>;
    Node* head;
    Node* tail;

    RefList(): head(nullptr), tail(nullptr) {}

    RefListIterator<T> begin() {
        return head ? RefListIterator<T>(head) : end();
    }
    RefListIterator<T> end() {
        return RefListIterator<T>::end();
    }

    Ptr pop() {
        if (head) {
            return remove(head);
        } else {
            return nullptr;
        }
    }

    void append(Ptr item) {
        // We take over the reference count held by 'item' as long as the item
        // is in the list.
        Node *node = latch(item.__ptr, nullptr);
        assert(!node->prev && !node->next);
        if (tail) {
            assert(head);
            node->prev = tail;
            tail->next = node;
            tail = node;
        } else {
            tail = head = node;
        }
    }

    Ptr remove(const Ptr& item) {
        Node *node = item.__ptr;
        return remove(node);
    }
    Ptr remove(Node* node) {
        auto prev = node->prev;
        auto next = node->next;
        node->prev = nullptr;
        if (prev) {
            prev->next = next;
        }
        node->next = nullptr;
        if (next) {
            next->prev = prev;
        }

        if (head == node) {
            head = next;
        }
        if (tail == node) {
            tail = prev;
        }

        // The return value takes over the reference held by the list
        Ptr res;
        res.__ptr = node;
        return res;
    }

    bool contains(Ptr item) const {
        Node *p = head;
        while (p) {
            if (p == item.__ptr) {
                return true;
            }
            p = p->next;
        }
        return false;
    }

};



