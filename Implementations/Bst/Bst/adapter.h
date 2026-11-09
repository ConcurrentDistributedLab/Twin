/**
 * Implementation of the lock-free external BST of Ellen, Fatourou, Ruppert and van Breugel.
 * This is a heavily modified version of the ASCYLIB implementation (see copyright in ellen.h).
 * The modifications are copyrighted (consistent with the original license)
 *   by Maya Arbel-Raviv and Trevor Brown, 2018.
 */

 #ifndef BST_ADAPTER_H
 #define BST_ADAPTER_H
 
 #include <iostream>
 #include <csignal>
 #include "config/errors.h"
 #include "ellen_impl.h"
 
 #define RECORD_MANAGER_T record_manager<node_t<K,V>, info_t<K,V>>
 #define DATA_STRUCTURE_T ellen<K, V, RECORD_MANAGER_T>
 
 template <typename K, typename V>
 class ds_adapter {
 private:
     const V NO_VALUE;
     DATA_STRUCTURE_T * const ds;
 
 public:
     ds_adapter(const int NUM_THREADS,
                const K& KEY_MIN,
                const K& KEY_MAX,
                const V& VALUE_RESERVED)
     : NO_VALUE(VALUE_RESERVED)
     , ds(new DATA_STRUCTURE_T(NUM_THREADS, KEY_MIN, KEY_MAX, NO_VALUE, 0 /* unused */))
     {}
     ~ds_adapter() {
         delete ds;
     }
 
     V getNoValue() {
         return NO_VALUE;
     }
 
     void initThread(const int tid) {
         ds->initThread(tid);
     }
     void deinitThread(const int tid) {
         ds->deinitThread(tid);
     }
 
     V insert(const int tid, const K& key, const V& val) {
         setbench_error("insert-replace functionality not implemented for this data structure");
     }
     V insertIfAbsent(const int tid, const K& key, const V& val) {
         return ds->bst_insert(tid, key, val);
     }
     V erase(const int tid, const K& key) {
         return ds->bst_delete(tid, key);
     }
     V eraseMin(const int tid) {
         return ds->bst_deleteMin(tid);
     }
     V eraseMax(const int tid) {
         return ds->bst_deleteMax(tid);
     }
     V find(const int tid, const K& key) {
         return ds->bst_find(tid, key);
     }
     bool contains(const int tid, const K& key) {
         return find(tid, key) != getNoValue();
     }
     int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
         setbench_error("not implemented");
     }
     void printSummary() { }
     bool validateStructure() {
         return true;
     }
     void printObjectSizes() {
         std::cout<<"sizes: node="
                  <<(sizeof(node_t<K,V>))
                  <<" descriptor="<<(sizeof(info_t<K,V>))
                  <<std::endl;
     }
     // try to clean up: must only be called by a single thread as part of the test harness!
     void debugGCSingleThreaded() { }

     unsigned int getTreeHeight() {
         return ds->get_tree_height();
     }

     void scan() {
        ds->scan();
     }
 
 };
 
 #endif