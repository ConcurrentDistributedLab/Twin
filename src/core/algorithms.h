// Library selection
#if defined(TWIN_LIST)
#include "../../Implementations/Twin/Twin_List/prioq.h"
typedef MinMaxQueue_s DataStructure;

#elif defined(CHROMATIC)
#include "../../Implementations/Chromatic/Chromatic/chromatic.h"
typedef long long test_type;
typedef record_manager<Node<test_type, test_type>,
                       SCXRecord<test_type, test_type>>
    RecordMgr;
typedef Chromatic<test_type, test_type, less<test_type>, RecordMgr>
    DataStructure;
#elif defined(CCCHROMATIC)
#include "../../Implementations/Chromatic/CCChromatic/chromatic.h"
typedef int64_t test_type;
typedef record_manager<Node<test_type, test_type>,
                       SCXRecord<test_type, test_type>>
    RecordMgr;
typedef Chromatic<test_type, test_type, less<test_type>, RecordMgr>
    DataStructure;

#elif defined(BST)
#include "../../Implementations/Bst/Bst/adapter.h"
#include <climits>
typedef ds_adapter<long long, void *> DataStructure;
#elif defined(CCBST)
#include "../../Implementations/Bst/CCBst/adapter.h"
#include <climits>
typedef ds_adapter<long long, void *> DataStructure;

#elif defined(LJ)
#include "../../Implementations/LJ/prioq.h"
typedef pq_t DataStructure;
#define ONLYMIN 1
#elif defined(TWIN_LJ)
#include "../../Implementations/Twin/Twin_LJ/prioq.h"
typedef pq_t DataStructure;

#elif defined(TSL)
#include "../../Implementations/Tsl/tslqueue.h"
typedef tsl_set_t DataStructure;
#define ONLYMIN 1
#elif defined(TWIN_TSL)
#include "../../Implementations/Twin/Twin_Tsl/tslqueue.h"
typedef tsl_set_t DataStructure;

#else
typedef void DataStructure;
#error "Priority queue library not defined"
#endif
