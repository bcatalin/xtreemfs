/*
 * Copyright (c) 2012-2013 by Jens V. Fischer, Zuse Institute Berlin
 *
 *
 * Licensed under the BSD License, see LICENSE file for details.
 *
 */

package org.xtreemfs.sandbox.benchmark;

import java.io.IOException;
import java.util.*;

import org.xtreemfs.common.libxtreemfs.AdminClient;
import org.xtreemfs.common.libxtreemfs.Volume;
import org.xtreemfs.common.libxtreemfs.exceptions.PosixErrorException;
import org.xtreemfs.foundation.logging.Logging;
import org.xtreemfs.foundation.pbrpc.generatedinterfaces.RPC.POSIXErrno;
import org.xtreemfs.pbrpc.generatedinterfaces.DIR;
import org.xtreemfs.pbrpc.generatedinterfaces.GlobalTypes;
import org.xtreemfs.pbrpc.generatedinterfaces.MRC;

/**
 * Volume Manager (Singleton).
 * <p/>
 * 
 * Class for managing volumes in the benchmark tools. Allows for the creation and deletion of volumes, manages the
 * assignment of volumes to benchmark threads and does bookkeeping of created files and volumes.
 * <p/>
 * The bookkeeping of created volumes is necessary for the deletion (only) of created volumes and files as well as for
 * the filebased benchmarks.
 * 
 * @author jensvfischer
 */
class VolumeManager {

    static final String              VOLUME_BASE_NAME = "benchmark";

    private static VolumeManager     volumeManager    = null;
    Config config;
    AdminClient                      client;
    int                              currentPosition;
    LinkedList<Volume>               volumes;
    LinkedList<Volume>               createdVolumes;
    HashMap<Volume, HashSet<String>> createdFiles;
    HashMap<Volume, String[]>        filelistsSequentialBenchmark;
    HashMap<Volume, String[]>        filelistsRandomBenchmark;

    /* init the VolumeManager with params. This only needs to be called once */
    static void init(Config config) throws Exception {
        if (volumeManager == null) {
            volumeManager = new VolumeManager(config);
        }
    }

    /* returns the (singleton) instance of the VolumeManager */
    static VolumeManager getInstance() throws Exception {
        if (volumeManager == null)
            throw new RuntimeException("Volume Manager not initialized");
        return volumeManager;
    }

    /* private constructor, used by init */
    private VolumeManager(Config config) throws Exception {
        this.config = config;
        currentPosition = 0;
        this.client = ClientManager.getNewClient(config);
        this.volumes = new LinkedList<Volume>();
        this.createdVolumes = new LinkedList<Volume>();
        this.filelistsSequentialBenchmark = new HashMap<Volume, String[]>(5);
        this.filelistsRandomBenchmark = new HashMap<Volume, String[]>(5);
        this.createdFiles = new HashMap<Volume, HashSet<String>>();
    }

    /* cycles through the list of volumes to assign to volumes to benchmarks */
    Volume getNextVolume() {
        return volumes.get(currentPosition++);
    }

    /*
     * reset the position counter of the volume list. Needs to be called if one wants to reuse the volumes for a
     * consecutive benchmark
     */
    void reset() {
        currentPosition = 0;
    }

    /* used if no volumes were specified */
    void createDefaultVolumes(int numberOfVolumes) throws IOException {
        for (int i = 0; i < numberOfVolumes; i++) {
            Volume volume = createAndOpenVolume(VOLUME_BASE_NAME + i);
            addToVolumes(volume);
        }
    }

    /* add a volume to the volume list */
    private void addToVolumes(Volume volume) {
        if (!volumes.contains(volume))
            volumes.add(volume);
    }

    /* opens multiple volumes (and creates and opens volumes if they do not exist) */
    void openVolumes(String... volumeName) throws IOException {
        this.volumes = new LinkedList<Volume>();
        for (String each : volumeName) {
            volumes.add(createAndOpenVolume(each));
        }
    }

    /* opens a single volume (or creates and opens a volume if it does not exist) */
    private Volume createAndOpenVolume(String volumeName) throws IOException {
        Volume volume = null;
        try {
            List<GlobalTypes.KeyValuePair> volumeAttributes = new ArrayList<GlobalTypes.KeyValuePair>();
            client.createVolume(config.auth, config.userCredentials, volumeName, 511, config.userName, config.group,
                    GlobalTypes.AccessControlPolicyType.ACCESS_CONTROL_POLICY_POSIX,
                    GlobalTypes.StripingPolicyType.STRIPING_POLICY_RAID0, config.getStripeSizeInKiB,
                    config.stripeWidth, volumeAttributes);
            volume = client.openVolume(volumeName, config.sslOptions, config.options);
            createdVolumes.add(volume);
            createDirStructure(volume);
            Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Created volume %s", volumeName);
        } catch (PosixErrorException e) {
            if (e.getPosixError() == POSIXErrno.POSIX_ERROR_EEXIST)
                volume = client.openVolume(volumeName, config.sslOptions, config.options);
            else
                throw e;
        }
        return volume;
    }

    private void createDirStructure(Volume volume) throws IOException {
        volume.createDirectory(config.userCredentials, "/benchmarks/sequentialBenchmark", 0777, true);
        volume.createDirectory(config.userCredentials, "/benchmarks/randomBenchmark", 0777, true);
    }

    /*
     * adds a filelist with files from a sequential benchmark (used to pass the list of files written by a sequential
     * write benchmark to a sequential read benchmark)
     */
    void setSequentialFilelistForVolume(Volume volume, LinkedList<String> filelist) {
        String[] files = new String[filelist.size()];
        synchronized (this) {
            filelistsSequentialBenchmark.put(volume, filelist.toArray(files));
        }
    }

    /*
     * adds a filelist with files from a filebased benchmark (used to pass the list of files written by a filebased
     * write benchmark to a filebased read benchmark)
     */
    void setRandomFilelistForVolume(Volume volume, LinkedList<String> filelist) {
        String[] files = new String[filelist.size()];
        synchronized (this) {
            filelistsRandomBenchmark.put(volume, filelist.toArray(files));
        }
    }

    /*
     * get the list of files written to a volume (used to pass the list of files written by a filebased write benchmark
     * to a filebased read benchmark)
     */
    synchronized String[] getSequentialFilelistForVolume(Volume volume) throws IOException {
        String[] filelist;

        /* null means no filelist from a previous write benchmark has been deposited */
        if (null == filelistsSequentialBenchmark.get(volume)) {
            filelist = inferFilelist(volume, SequentialBenchmark.BENCHMARK_FILENAME);

            if (config.sequentialSizeInBytes == calculateTotalSizeOfFilelist(volume, filelist)) {
                filelistsSequentialBenchmark.put(volume, filelist);
                Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this,
                        "Succesfully infered filelist on volume %s.", volume.getVolumeName());
            } else {
                Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Infering filelist failed",
                        volume.getVolumeName());
                throw new IllegalArgumentException("No valid files for benchmark found");
            }

        }
        return filelistsSequentialBenchmark.get(volume);
    }

    /*
     * get the list of files written to a volume (used to pass the list of files written by a filebased write benchmark
     * to a filebased read benchmark)
     */
    synchronized String[] getRandomFilelistForVolume(Volume volume) throws IOException {
        String[] filelist;

        /* null means no filelist from a previous write benchmark has been deposited */
        if (null == filelistsRandomBenchmark.get(volume)) {
            filelist = inferFilelist(volume, FilebasedBenchmark.BENCHMARK_FILENAME);

            if (config.randomSizeInBytes == calculateTotalSizeOfFilelist(volume, filelist)) {
                filelistsRandomBenchmark.put(volume, filelist);
                Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this,
                        "Succesfully infered filelist on volume %s.", volume.getVolumeName());
            } else {
                Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Infering filelist failed",
                        volume.getVolumeName());
                throw new IllegalArgumentException("No valid files for benchmark found");
            }

        }
        return filelistsRandomBenchmark.get(volume);
    }

    /*
     * Tries to infer a list of files from sequential benchmarks. This is called if a sequential read benchmark was
     * executed without a previous write benchmark (e.g. when the benchmark tool is executed first to do write
     * benchmarks with the noCleanup option, a consecutive renewed execution of a read benchmark tries to infer the
     * filelist)
     */
    private String[] inferFilelist(Volume volume, String pathToBasefile) throws IOException {

        Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Read benchmark without write benchmark. "
                + "Trying to infer a filelist on volume %s", volume.getVolumeName());

        String path = pathToBasefile.substring(0, pathToBasefile.lastIndexOf('/'));
        String filename = pathToBasefile.substring(pathToBasefile.lastIndexOf('/') + 1);

        List<MRC.DirectoryEntry> directoryEntries = volume.readDir(config.userCredentials, path, 0, 0, true)
                .getEntriesList();
        ArrayList<String> entries = new ArrayList<String>(directoryEntries.size());

        for (MRC.DirectoryEntry directoryEntry : directoryEntries) {
            String entry = directoryEntry.getName();
            if (entry.matches(filename + "[0-9]+"))
                entries.add(path + '/' + directoryEntry.getName());
        }
        entries.trimToSize();
        String[] filelist = new String[entries.size()];
        entries.toArray(filelist);
        return filelist;
    }

    /* calculates the number of files on a volume */
    private long calculateTotalSizeOfFilelist(Volume volume, String[] filelist) throws IOException {
        long aggregatedSizeInBytes = 0;
        for (String file : filelist) {
            MRC.Stat stat = volume.getAttr(config.userCredentials, file);
            aggregatedSizeInBytes += stat.getSize();
        }
        return aggregatedSizeInBytes;
    }

    /* adds the files created by a benchmark to the list of created files */
    synchronized void addCreatedFiles(Volume volume, LinkedList<String> newFiles) {
        HashSet<String> filelistForVolume;
        if (createdFiles.containsKey(volume))
            filelistForVolume = createdFiles.get(volume);
        else
            filelistForVolume = new HashSet();
        filelistForVolume.addAll(newFiles);
        createdFiles.put(volume, filelistForVolume);
    }

    /* deletes all files on in the createdFiles list */
    void deleteCreatedFiles() {
        for (Volume volume : volumes) {
            HashSet<String> fileListForVolume = createdFiles.get(volume);

            if (null != fileListForVolume) {

                Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Deleted %s file(s) on volume %s",
                        fileListForVolume.size(), volume.getVolumeName());

                for (String filename : fileListForVolume) {
                    tryToDeleteFile(volume, filename);
                }
            }
        }
    }

    /* try to delete a file. log errors, but continue */
    private void tryToDeleteFile(Volume volume, String filename) {
        try {
            volume.unlink(config.userCredentials, filename);
        } catch (IOException e) {
            Logging.logMessage(Logging.LEVEL_ERROR, Logging.Category.tool, this,
                    "IO Error while trying to delete a file.");
            Logging.logError(Logging.LEVEL_ERROR, Logging.Category.tool, e);
        }
    }

    /* deletes all volumes in the list of created volumes */
    void deleteCreatedVolumes() throws IOException {
        for (Volume volume : createdVolumes) {
            deleteVolumeIfExisting(volume);
        }
    }

    /* deletes the given volumes */
    void deleteVolumes(String... volumeName) throws IOException {
        for (String each : volumeName) {
            deleteVolumeIfExisting(each);
        }
    }

    /* delete the default volumes */
    void deleteDefaultVolumes(int numberOfVolumes) throws IOException {
        for (int i = 0; i < numberOfVolumes; i++) {
            deleteVolumeIfExisting(VOLUME_BASE_NAME + i);
        }
    }

    /* delete a volume specified by a volume ref */
    void deleteVolumeIfExisting(Volume volume) throws IOException {
        deleteVolumeIfExisting(volume.getVolumeName());
    }

    /* delete a volume specified by a string with the volumes name */
    void deleteVolumeIfExisting(String volumeName) throws IOException {
        if (new ArrayList<String>(Arrays.asList(client.listVolumeNames())).contains(volumeName)) {
            client.deleteVolume(config.auth, config.userCredentials, volumeName);
            Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Deleted volume %s", volumeName);
        }
    }

    /* Performs cleanup on a OSD (because deleting the volume does not delete the files in the volume) */
    void cleanupOSD() throws Exception {

        String pwd = config.osdPassword;
        LinkedList<String> uuids = getOSDUUIDs();

        for (String osd : uuids) {
            Logging.logMessage(Logging.LEVEL_INFO, Logging.Category.tool, this, "Starting cleanup of OSD %s", osd);
            client.startCleanUp(osd, pwd, true, true, false);
        }

        boolean cleanUpIsRunning = true;
        while (cleanUpIsRunning) {
            cleanUpIsRunning = false;
            for (String osd : uuids) {
                cleanUpIsRunning = cleanUpIsRunning || client.isRunningCleanUp(osd, pwd);
            }
            Thread.sleep(300);
        }

        for (String osd : uuids) {
            Logging.logMessage(Logging.LEVEL_DEBUG, Logging.Category.tool, this, "Finished cleanup. Result: %s",
                    client.getCleanUpResult(osd, pwd));
        }

    }

    /* get the list of all OSDs registered a the DIR */
    LinkedList<String> getOSDUUIDs() throws IOException {
        LinkedList<String> uuids = new LinkedList<String>();
        for (DIR.Service service : client.getServiceByType(DIR.ServiceType.SERVICE_TYPE_OSD).getServicesList()) {
            uuids.add(service.getUuid());
        }
        return uuids;
    }

    /* get the list of volumes */
    LinkedList<Volume> getVolumes() {
        return volumes;
    }

}
