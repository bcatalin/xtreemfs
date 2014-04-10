/*
 * Copyright (c) 2012-2013 by Jens V. Fischer, Zuse Institute Berlin
 *               
 *
 * Licensed under the BSD License, see LICENSE file for details.
 *
 */

package org.xtreemfs.common.benchmark;

import org.xtreemfs.common.libxtreemfs.Options;
import org.xtreemfs.foundation.SSLOptions;
import org.xtreemfs.foundation.pbrpc.generatedinterfaces.RPC;

import java.util.HashMap;
import java.util.Map;

import static org.xtreemfs.foundation.pbrpc.client.RPCAuthentication.authNone;

/**
 * Builder for the {@link Config} datastructure.
 * <p/>
 * 
 * Use like this: <br/>
 * <code>
 * ParamsBuilder builder = new ParamsBuilder();<br/>
 * builder.setX("x"); <br/>
 * builder.setY("y"); <br/>
 * Params params = builder.build(); <br/>
 * </code> or like this <br/>
 * <code>
 * Params params = new ParamsBuilder().setX("x").setY("y").build();<br/>
 * </code>
 * 
 * The {@link Controller}, the {@link ConfigBuilder} and {@link Config} represent the API to the benchmark library.
 * 
 * @author jensvfischer
 */
public class ConfigBuilder {

    private long                basefileSizeInBytes   = 3L * BenchmarkUtils.GiB_IN_BYTES;
    private int                 filesize              = 4 * BenchmarkUtils.KiB_IN_BYTES;
    private String              userName              = "benchmark";
    private String              group                 = "benchmark";
    private String              adminPassword         = "";
    private String              dirAddress            = "127.0.0.1:32638";
    private RPC.Auth            auth                  = authNone;
    private SSLOptions          sslOptions            = null;
    private Options             options               = new Options();
    private String              osdSelectionPolicies  = "";
    private Map<String, String> policyAttributes      = new HashMap<String, String>();
    private int                 chunkSizeInBytes      = 128 * BenchmarkUtils.KiB_IN_BYTES;
    private int                 stripeSizeInBytes     = 128 * BenchmarkUtils.KiB_IN_BYTES;
    private boolean             stripeSizeSet         = false;
    private int                 stripeWidth           = 1;
    private boolean             stripeWidthSet        = false;
    private boolean             noCleanup             = false;
    private boolean             noCleanupOfVolumes    = false;
    private boolean             noCleanupOfBasefile   = false;
    private boolean             osdCleanup            = false;

    /**
     * Instantiate an builder (all values are the default values, see {@link Config}).
     */
    public ConfigBuilder() {
    }

    /**
     * Set the size of the basefile for random benchmarks. <br/>
     * Default: 3 GiB.
     * 
     * @param basefileSizeInBytes
     * @return the builder
     */
    public ConfigBuilder setBasefileSizeInBytes(long basefileSizeInBytes) {
        if (basefileSizeInBytes < 1)
            throw new IllegalArgumentException("basefileSizeInBytes < 1 not allowed");
        this.basefileSizeInBytes = basefileSizeInBytes;
        return this;
    }

    /**
     * Set the size of files in filebased benchmark. <br/>
     * Default: 4 KiB.
     * 
     * @param filesize
     * @return the builder
     */
    public ConfigBuilder setFilesize(int filesize) {
        if (filesize < 1)
            throw new IllegalArgumentException("filesize < 1 not allowed");
        this.filesize = filesize;
        return this;
    }

    /**
     * Set the username to be used when creating files and volumes <br/>
     * Default: benchmark.
     * 
     * @param userName
     * @return the builder
     */
    public ConfigBuilder setUserName(String userName) {
        if (userName.isEmpty())
            throw new IllegalArgumentException("Empty username not allowed");
        this.userName = userName;
        return this;
    }

    /**
     * Set the group to be used when creating files and volumes. <br/>
     * Default: benchmark.
     * 
     * @param group
     * @return the builder
     */
    public ConfigBuilder setGroup(String group) {
        if (group.isEmpty())
            throw new IllegalArgumentException("Empty group name not allowed");
        this.group = group;
        return this;
    }

    /**
     * Set the password for accessing the osd(s) <br/>
     * Default: "".
     * 
     * @param adminPassword
     * @return the builder
     */
    public ConfigBuilder setAdminPassword(String adminPassword) {
        this.adminPassword = adminPassword;
        return this;
    }

    /**
     * Set the address of the DIR Server. <br/>
     * Default: 127.0.0.1:3263
     * 
     * @param dirAddress
     * @return the builder
     */
    public ConfigBuilder setDirAddress(String dirAddress) {
        /* remove protocol information */
        if (dirAddress.contains("://"))
            dirAddress = dirAddress.split("://", 2)[1];
        /* remove trailing slashes */
        if (dirAddress.endsWith("/"))
            dirAddress = dirAddress.substring(0, dirAddress.length()-1);
        this.dirAddress = dirAddress;
        return this;
    }

    /**
     * Set the RPC user credentials. Build from the user name and group name during instatiation of {@link Config}.
     * 
     * @param auth
     * @return the builder
     */
    public ConfigBuilder setAuth(RPC.Auth auth) {
        this.auth = auth;
        return this;
    }

    /**
     * Set the SSL options for SSL Authetification Provider. <br/>
     * Default: null.
     * 
     * @param sslOptions
     * @return the builder
     */
    public ConfigBuilder setSslOptions(SSLOptions sslOptions) {
        this.sslOptions = sslOptions;
        return this;
    }

    /**
     * Set the libxtreemfs {@link org.xtreemfs.common.libxtreemfs.Options}.
     * 
     * @param options
     * @return the builder
     */
    public ConfigBuilder setOptions(Options options) {
        this.options = options;
        return this;
    }

    /**
     * Set the OSD selection policies used when creating or opening volumes. <br/>
     *
     * Default: No policy is set. If an existing volume is used this means, that already set policies of the volume are
     * used. If a new volume is created, the defaults are use ("1000,3002": OSD filter, Shuffling).
     * 
     * @param policies
     * @return the builder
     */
    public ConfigBuilder setOsdSelectionPolicies(String policies) {
         this.osdSelectionPolicies = policies;
        return this;
    }

    /**
     * Set a policy attribute for a OSD selection policies. <p/>
     * This method can be called multiple times, if multiple attributes are to be set. <br/>
     * The attributes are set when the volumes are created / opened. <br/>
     * A policy attribute consists of the name of the attribute, and the value the attribute is set to. For more information see the XtreemFS User Guide. <br/>
     *
     * Attribute Format: <policy id>.<attribute name> e.g., "1002.uuids" <br/>
     * Value format: <value>, e.g. "osd01" 
     * 
     * @param attribute the attribute to be set
     * @param value the value the attribute is set to
     * @return the builder
     */
    public ConfigBuilder setPolicyAttribute(String attribute, String value) {
        this.policyAttributes.put(attribute, value);
        return this;
    }

    /**
     * Set the UUID-based filter policy (ID 1002) as OSD selection policy and set the uuids to be used by the policy
     * (applied when creating/opening the volumes). It is a shortcut for setting the policy and the attributes manually. <br/>
     *
     * Default: see {@link #setOsdSelectionPolicies(String)}.
     * 
     * @param uuids
     *            the uuids of osds to be used
     * @return the builder
     */
    public ConfigBuilder setSelectOsdsByUuid(String uuids) {
        if (this.osdSelectionPolicies.equals(""))
            this.osdSelectionPolicies = "1002";
        else
            this.osdSelectionPolicies += ",1002";
        this.policyAttributes.put("1002.uuids", uuids);
        return this;
    }

    /**
     * Set the chunk size for reads/writes in benchmarks. The chuck size is the amount of data written/ret in one piece. <br/>
     * 
     * Default: 131072 bytes (128 KiB).
     *
     * @param chunkSizeInBytes the chunk size in bytes
     * @return the builder
     */
    public ConfigBuilder setChunkSizeInBytes(int chunkSizeInBytes) {
        this.chunkSizeInBytes = chunkSizeInBytes;
        return this;
    }

    /**
     * Set the size of an OSD storage block ("blocksize") in bytes when creating or opening volumes. <br/>
     * 
     * When opening existing volumes, by default, the stripe size of the given volume is used. When creating a new
     * volume, by default a stripe size of 131072 bytes (128 KiB) is used. <br/>
     * 
     * @param stripeSizeInBytes
     *            the stripe size in bytes
     * @return the builder
     */
    public ConfigBuilder setStripeSizeInBytes(int stripeSizeInBytes) {
        this.stripeSizeInBytes = stripeSizeInBytes;
        this.stripeSizeSet = true;
        return this;
    }

    /**
     * Set the maximum number of OSDs a file is distributed to. Used when creating or opening volumes <br/>
     *
     * When opening existing volumes, by default, the stripe width of the given volume is used. When creating a new
     * volume, by default a stripe width of 1 is used. <br/>
     * 
     * @param stripeWidth
     * @return the builder
     */
    public ConfigBuilder setStripeWidth(int stripeWidth) {
        this.stripeWidth = stripeWidth;
        this.stripeWidthSet = true;
        return this;
    }

    /**
     * If set, the files and volumes created during the benchmarks will not be deleted. <br/>
     * Default: false.
     *
     * @return the builder
     */
    public ConfigBuilder setNoCleanup() {
        this.noCleanup = true;
        return this;
    }

    /**
     * If set, the volumes created during the benchmarks will not be deleted. <br/>
     * Default: false.
     * 
     * @return the builder
     */
    public ConfigBuilder setNoCleanupOfVolumes() {
        this.noCleanupOfVolumes = true;
        return this;
    }

    /**
     * If set, a basefile created during benchmarks will not be deleted. <br/>
     * Default: false.
     * 
     * @return the builder
     */
    public ConfigBuilder setNoCleanupOfBasefile() {
        this.noCleanupOfBasefile = true;
        return this;
    }

    /**
     * If set, a OSD Cleanup will be done at the end of all benchmarks. This might be needed to actually delete
     * the storage blocks from the OSD after deleting volumes. <br/>
     * Default: false.
     *
     * @return the builder
     */
    public ConfigBuilder setOsdCleanup() {
        this.osdCleanup = true;
        return this;
    }

    long getBasefileSizeInBytes() {
        return basefileSizeInBytes;
    }

    int getFilesize() {
        return filesize;
    }

    String getUserName() {
        return userName;
    }

    String getGroup() {
        return group;
    }

    String getAdminPassword() {
        return adminPassword;
    }

    String getDirAddress() {
        return dirAddress;
    }

    RPC.Auth getAuth() {
        return auth;
    }

    SSLOptions getSslOptions() {
        return sslOptions;
    }

    Options getOptions() {
        return options;
    }

    String getOsdSelectionPolicies() {
        return osdSelectionPolicies;
    }

    Map<String, String> getPolicyAttributes() {
        return policyAttributes;
    }

    int getChunkSizeInBytes() {
        return chunkSizeInBytes;
    }

    int getStripeSizeInBytes() {
        return stripeSizeInBytes;
    }

    boolean isStripeSizeSet() {
        return stripeSizeSet;
    }

    int getStripeWidth() {
        return stripeWidth;
    }

    boolean isStripeWidthSet() {
        return stripeWidthSet;
    }

    boolean isNoCleanup() {
        return noCleanup;
    }

    boolean isNoCleanupOfVolumes() {
        return noCleanupOfVolumes;
    }

    boolean isNoCleanupOfBasefile() {
        return noCleanupOfBasefile;
    }

    boolean isOsdCleanup() {
        return osdCleanup;
    }

    /**
     * Build the {@link Config} object.
     * 
     * @return the build {@link Config} object
     * @throws Exception
     */
    public Config build() throws Exception {
        verifyNoCleanup();
        return new Config(this);
    }

    private void verifyNoCleanup() {
        if (noCleanupOfBasefile && !noCleanup && !noCleanupOfVolumes)
            throw new IllegalArgumentException("noCleanupOfBasefile only works with noCleanup or noCleanupVolumes");
    }
}
