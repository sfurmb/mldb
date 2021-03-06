/** eval_sql_test.cc
    Jeremy Barnes, 3 August 2016
    Copyright (c) 2016 Datacratic Inc.  All rights reserved.

    This file is part of MLDB. Copyright 2016 Datacratic. All rights reserved.

    Test of sql evaluation.
*/

#include "mldb/sql/binding_contexts.h"
#include "mldb/sql/eval_sql.h"

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include <tuple>
#include <iostream>

using namespace std;
using namespace Datacratic;
using namespace Datacratic::MLDB;

BOOST_AUTO_TEST_CASE(test_element_compare)
{
    SqlBindingScope scope;

    BOOST_CHECK_EQUAL(evalSql(scope, "$1 + $2", 1, 2).getAtom(), 3);
    BOOST_CHECK_EQUAL(evalSql(scope, "$1 * $2", 5, 20).getAtom(), 100);
    BOOST_CHECK_EQUAL(evalSql(scope, "[1, 2] + [3, 4]").extractJson().toString(),
                      "[4,6]\n");
}
