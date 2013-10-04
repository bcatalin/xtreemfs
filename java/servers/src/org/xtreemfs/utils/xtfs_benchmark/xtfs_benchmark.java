/*
 * Copyright (c) 2012-2013 by Jens V. Fischer, Zuse Institute Berlin
 *
 *
 * Licensed under the BSD License, see LICENSE file for details.
 *
 */
package org.xtreemfs.utils.xtfs_benchmark;

import static org.xtreemfs.foundation.logging.Logging.Category;

import java.util.concurrent.ConcurrentLinkedQueue;

import org.xtreemfs.foundation.logging.Logging;
import org.xtreemfs.common.benchmark.BenchmarkResult;
import org.xtreemfs.common.benchmark.Config;
import org.xtreemfs.common.benchmark.Controller;

/**
 * The commandline benchmark tool.
 */
public class xtfs_benchmark {

    static final int KiB_IN_BYTES = 1024;
    static final int MiB_IN_BYTES = 1024 * 1024;
    static final int GiB_IN_BYTES = 1024 * 1024 * 1024;

    private static Controller controller;
    private static CLIOptions cliOptions;

    static {
        cliOptions = new CLIOptions();
        Thread.setDefaultUncaughtExceptionHandler(new UncaughtExceptionHandlerBenchmark());
    }

    /**
     * Main loop of the commandline tool.
     * 
     * @param args
     *            commandline options for the tool, as specified in {@link CLIOptions}
     * @throws Exception
     */
    public static void main(String[] args) throws Exception {

        Logging.start(6, Category.tool);
        Logging.redirect(System.err);

        cliOptions.parseCLIOptions(args);

        if (cliOptions.usageIsSet()) {
            cliOptions.displayUsage();
            return;
        }

        Config config = cliOptions.buildParamsFromCLIOptions();
        controller = new Controller(config);
        controller.tryConnection();
        controller.setupVolumes(cliOptions.getArguments().toArray(new String[cliOptions.getArguments().size()]));
        runBenchmarks(config);
        controller.teardown();
    }

    /* run all benchmarks specified by the CLIOptions */
    private static void runBenchmarks(Config config) throws Exception {

        ConcurrentLinkedQueue<BenchmarkResult> result;
        ConcurrentLinkedQueue<BenchmarkResult> results = new ConcurrentLinkedQueue<BenchmarkResult>();

        if (cliOptions.sequentialWriteBenchmarkIsSet()) {
            result = controller.startSequentialWriteBenchmark();
            results.addAll(result);
        }

        if (cliOptions.sequentialReadBenchmarkIsSet()) {
            result = controller.startSequentialReadBenchmark();
            results.addAll(result);
        }

        if (cliOptions.randomWriteBenchmarkIsSet()) {
            result = controller.startRandomWriteBenchmark();
            results.addAll(result);
        }

        if (cliOptions.randomReadBenchmarkIsSet()) {
            result = controller.startRandomReadBenchmark();
            results.addAll(result);
        }

        if (cliOptions.randomFilebasedWriteBenchmarkIsSet()) {
            result = controller.startFilebasedWriteBenchmark();
            results.addAll(result);
        }

        if (cliOptions.randomFilebasedReadBenchmarkIsSet()) {
            result = controller.startFilebasedReadBenchmark();
            results.addAll(result);
        }

        printResults(results);
        printResultsCSV(results);
    }

    /* Print the results as csv. */
    private static void printResultsCSV(ConcurrentLinkedQueue<BenchmarkResult> results) {
        System.out.println("Type;NumberOfParallelThreads;TimeInSec;MiB/Sec;DataWrittenInBytes;ByteCount");
        /* print the results */
        for (BenchmarkResult res : results) {
            System.out.println(resultToCSV(res));
        }
    }

    /* Print the results in a json like style. */
    private static void printResults(ConcurrentLinkedQueue<BenchmarkResult> results) {
        /* print the results */
        for (BenchmarkResult res : results) {
            System.err.println(resultToString(res));
        }
    }

    /* convert a single result to json like String */
    private static String resultToString(BenchmarkResult result) {

        String dataWritten = result.getDataRequestedInBytes() >= GiB_IN_BYTES ? result.getDataRequestedInBytes()/ GiB_IN_BYTES + " GiB ["
                : result.getDataRequestedInBytes()/ MiB_IN_BYTES + " MiB [";
        String readersOrWriters;
        
        if (result.isWriteBenchmark()) {
            readersOrWriters = "\tNumber of Writers: " + result.getNumberOfReadersOrWriters() + "\n";
        } else if (result.isReadBenchmark()) {
            readersOrWriters = "\tNumber of Readers: " + result.getNumberOfReadersOrWriters() + "\n";
        } else {
            readersOrWriters = "\tNumber of Readers/Writers: " + result.getNumberOfReadersOrWriters() + "\n";
        }

        return "{\n\tBenchmarkType: " + result.getBenchmarkType() + "\n" + readersOrWriters
                + "\tTime: " + result.getTimeInSec() + " Sec\n" + "\tSpeed: " + result.getSpeedInMiBPerSec() + " MiB/s\n" + "\tData written: "
                + dataWritten + result.getDataRequestedInBytes()+ " Bytes]\n" + "\tByteCount: " + result.getByteCount() + " Bytes\n" + "}";
    }

    /* convert a single result to csv */
    private static String resultToCSV(BenchmarkResult result) {
        return result.getBenchmarkType() + ";" + result.getNumberOfReadersOrWriters() + ";"
                + result.getTimeInSec() + ";" + result.getSpeedInMiBPerSec() + ";" + result.getDataRequestedInBytes() + ";"
                + result.getByteCount();
    }
}
