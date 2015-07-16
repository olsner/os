template <class K, class V> struct DictNode;
template <typename T>
DictNode<typename T::Key, T> *node_from_item(T *c) { return &c->node; }

template <class K, class V> struct DictNode
{
    K key;
    DictNode* left;
    DictNode* right;

    DictNode(K key): key(key), left(nullptr), right(nullptr) {}

    static DictNode *node(V *item) {
        return node_from_item(item);
    }
    static intptr_t node_offset() {
        return (intptr_t)node((V*)0);
    }
    V* item() {
        return (V*)((u8*)this - node_offset());
    }
};
template <class V, class K = typename V::Key> struct Dict
{
    typedef DictNode<K, V> Node;
    Node* root;

    Dict(): root(nullptr) {}

    V* find(K key) const {
        Node *node = root;
        while (node) {
            if (node->key == key) {
                return node->item();
            }
            node = node->right;
        }
        return NULL;
    }

    V *insert(V* item) {
        Node *node = Node::node(item);
        node->right = root;
        root = node;
        return item;
    }

    void remove(V* item) {
        remove(Node::node(item)->key);
    }
    void remove(K key) {
        Node **p = &root;
        while (Node *node = *p) {
            if (node->key == key) {
                *p = node = node->right;
                return;
            }
            p = &node->right;
        }
        assert(!"Removing non-existing item");
    }
};

#define DICT_NODE(Class, member) \
    DictNode<Class::Key, Class> *node_from_item(Class *c) { return &c->member; }

