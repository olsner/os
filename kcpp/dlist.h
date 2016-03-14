template <class T> struct DListNode;
template <typename T>
DListNode<T> *dlistnode_from_item(T *c) { return &c->node; }

template <class T> struct DListNode
{
    T* prev;
    T* next;

    DListNode(): prev(nullptr), next(nullptr) {}

    static DListNode *node(T *item) {
        return dlistnode_from_item(item);
    }
    static intptr_t node_offset() {
        return (intptr_t)node((T*)0);
    }
    T* item() {
        return (T*)((u8*)this - node_offset());
    }
};
template <class T> struct DList
{
    typedef DListNode<T> Node;
    T* head;
    T* tail;

    DList(): head(nullptr), tail(nullptr) {}

    // using Node::node?
    static Node *node(T *item) {
        return Node::node(item);
    }

    T *pop() {
        if (head) {
            return remove(head);
        } else {
            return nullptr;
        }
    }

    T *append(T* item) {
        assert(!node(item)->prev && !node(item)->next);
        if (tail) {
            assert(head);
            node(item)->prev = tail;
            node(tail)->next = item;
            tail = item;
        } else {
            tail = head = item;
        }
        return item;
    }

    T *remove(T* item) {
        auto prev = node(item)->prev;
        auto next = node(item)->next;
        node(item)->prev = nullptr;
        if (prev) {
            node(prev)->next = next;
        }
        node(item)->next = nullptr;
        if (next) {
            node(next)->prev = prev;
        }

        if (head == item) {
            head = next;
        }
        if (tail == item) {
            tail = prev;
        }
        return item;
    }
};

#define DLIST_NODE(Class, member) \
    DListNode<Class> *dlistnode_from_item(Class *c) \
    { return &c->member; }



