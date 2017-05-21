template <class K, class V> struct DictNode;
template <typename T>
DictNode<typename T::Key, T> *node_from_item(T *c) { return &c->node; }

template <class K, class V> struct DictNode
{
    K key;
    //DictNode* left;
    DictNode* right;

    DictNode(K key):
        key(key),
        //left(nullptr),
        right(nullptr) {}

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

    // Return the greatest item with key <= key
    V* find_le(K key) const {
        Node *max = NULL;
        Node *node = root;
        while (node) {
            log(dict_find,
                "find(%#lx): node %p (%#lx) right %p max %p (%#lx)\n",
                key, node, node->key, node->right, max, max ? max->key : 0);
            if (node->key <= key && (!max || node->key > max->key)) {
                max = node;
            }
            node = node->right;
        }
        return max ? max->item() : NULL;
    }

    V* find_exact(K key) const {
        Node *node = root;
        while (node) {
            if (node->key == key) {
                return node->item();
            }
            node = node->right;
        }
        return NULL;
    }

    bool contains(V* item) const {
        Node *node = root;
        while (node) {
            if (node == node_from_item(item)) {
                return true;
            }
            node = node->right;
        }
        return false;
    }

    V *insert(V* item) {
        Node *node = node_from_item(item);
        log(dict_insert, "insert(%p): key %#lx root %p (%#lx)\n",
            item, node->key, root, root ? root->key : 0);
        node->right = root;
        root = node;
        return item;
    }

    void rekey(V* item, K key) {
        // This dictionary is still very stupid, so no need to update a sort
        // tree or anything.
        node_from_item(item)->key = key;
    }

    WARN_UNUSED_RESULT V* remove(V* item) {
        if (Node *n = remove(node_from_item(item)->key)) {
            return n->item();
        } else {
            return NULL;
        }
    }
    WARN_UNUSED_RESULT V *remove(K key) {
        Node **p = &root;
        while (Node *node = *p) {
            if (node->key == key) {
                *p = node->right;
                return node->item();
            }
            p = &node->right;
        }
        return NULL;
    }
    WARN_UNUSED_RESULT V *pop() {
        Node **p = &root;
        if (Node *node = *p) {
            *p = node->right;
            return node->item();
        }
        return NULL;
    }

    // Find and remove one item with start < key < end and return it to the
    // caller (which takes over ownership).
    WARN_UNUSED_RESULT V *remove_range_exclusive(K start, K end) {
        Node **p = &root;
        while (Node *node = *p) {
            if (start < node->key && node->key < end) {
                *p = node->right;
                return node->item();
            }
            p = &node->right;
        }
        return NULL;
    }
};

#define DICT_NODE(Class, member) \
    DictNode<Class::Key, Class> *node_from_item(Class *c) { return &c->member; }

