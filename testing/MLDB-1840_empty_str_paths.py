#
# MLDB-1840_empty_str_paths.py
# Francois Maillet, 2016-07-21
# This file is part of MLDB. Copyright 2016 Datacratic. All rights reserved.
#

mldb = mldb_wrapper.wrap(mldb)  # noqa

class Mldb1840EmptyStrPaths(MldbUnitTest):  # noqa

    def test_parse_empty_col_name(self):
#         mldb.put('/v1/datasets/ds', {'type' : 'sparse.mutable'})
#         mldb.post('/v1/datasets/ds/rows', {'rowName' : 'row', 'columns' : [['', 'val', 0]]})
#         mldb.post('/v1/datasets/ds/commit')
        self.assertTableResultEquals(
            mldb.query("""
                SELECT parse_json('{"": 5, "pwet":10}') AS *
            """),
            [
                ["_rowName", '""', "pwet"],
                [  "result",  5, 10]
            ]
        )

    def test_select_star_subselect_with_empty_col_name(self):
        self.assertTableResultEquals(
            mldb.query("""
                SELECT * FROM (
                    SELECT parse_json('{"": 5, "pwet":10}') AS *
                )
            """),
            [
                ["_rowName", '""', "pwet"],
                [  "result",  5, 10]
            ]
        )

    def test_select_named_col_subselect_with_empty_col_name(self):
        self.assertTableResultEquals(
            mldb.query("""
                SELECT pwet FROM (
                    SELECT parse_json('{"": 5, "pwet":10}') AS *
                )
            """),
            [
                ["_rowName", "pwet"],
                [  "result", 10]
            ]
        )

    def test_select_empty_col_subselect_with_empty_col_name(self):
        self.assertTableResultEquals(
            mldb.query("""
                SELECT "" FROM (
                    SELECT parse_json('{"": 5, "pwet":10}') AS *
                )
            """),
            [
                ["_rowName", '""'],
                [  "result", 5]
            ]
        )


if __name__ == '__main__':
    mldb.run_tests()