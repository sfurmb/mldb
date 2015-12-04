// This file is part of MLDB. Copyright 2015 Datacratic. All rights reserved.

// test plugin for MLDB

function handleStatus()
{
    // Hello this is a comment
    plugin.log("handling status");
    throw "exception in status";
}

plugin.setStatusHandler(handleStatus);
