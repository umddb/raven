# Experiments

This file outlines how to run the experiments on Ubuntu 20.04 or Ubuntu 22.04 on x86-64 machines. For the following experiments, custom versions of Iceberg 1.10, Delta Lake Kernel 4.0 and Raven were set up on a cluster of 4 nodes.

## Set up
On all nodes, download Open JDK 17

```bash
sudo apt install openjdk-17-jdk
```

On the node that will run Catalog Service (Node 2) and the node that will run the Garbage Collector (Node 3), download the Raven repo to build Raven: 
```bash
cd <raven_path>/src/thirdparty
./install.sh
cd ../..
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

On both nodes, create a config file for the systems. An example config.txt file is under catalogservice/. The configs are as follows:
* postgres.host : The host address of the Metadata Store (Postgres).
* postgres.user : Postgres user
* postgres.password : Postgres password
* postgres.dbname : Postgres database name
* postgres.connect_timeout : Postgres connect time out
* gc.epoch_period : Garbage collector epoch period in milliseconds.
* gc.num_file_task_ingestors : Number of workers that create file tasks in the garbage collector.
* gc.num_file_task_executors : Number of workers that execute file tasks in the garbage collector.
* gc.graph.path : The path of the RocksDB instance for graph store.
* gc.graph.create : Whether to create a new graph store.
* gc.graph.max_pages : The maximum size of page index for graph analytic engine.
* gc.graph.num_rc_workers : Number of workers that perform reference counting.
* gc.metrics_output : The output path of metrics (files deleted) for the garbage collector.
* grpc.num_workers : Number of workers for the gRPC server of the catalog service.
* grpc.server_address : The address of the gRPC server
* catalog.num_workers : Number of workers of the catalog service.
* aws.region : AWS region for S3.
* aws.access : AWS access key for S3.
* aws.secret : AWS secret key for S3.

On the node (Node 1) that will run Metadata Store, download Postgres and set up credentials:
```bash
sudo apt update
sudo apt install postgresql postgresql-contrib

sudo -i -u postgres psql

CREATE USER "koh" WITH PASSWORD 'koh';
CREATE DATABASE "koh" OWNER "koh";
GRANT ALL PRIVILEGES ON DATABASE "koh" TO "koh";

\c koh

ALTER SCHEMA public OWNER TO "koh";
GRANT ALL ON SCHEMA public TO "koh";
```

Then, run <raven_path>/experiments/metadatastore/catalog.sql


On the last node (Node 4), clone Delta Lake and Iceberg, apply the git patches, and build:
```bash
git clone --branch branch-4.0 --single-branch git@github.com:delta-io/delta.git
cd delta
git checkout bf6b370
git apply <raven_path>/experiments/delta4.0.patch
./build/sbt gatherAllJars
cd ..

git clone --branch 1.10.x --single-branch git@github.com:apache/iceberg.git
cd iceberg
git checkout ccb8bc4
git apply <raven_path>/experiments/iceberg1.10.patch
./gradlew :iceberg-exp:installDist
cd ..
```

On the same node, set up an embedded Hive instance (Hive 3.1.3 and Hadoop 3.4.1) for Iceberg.

## ExtendedMoR Write Experiment1
This experiment runs a microbenchmark to evaluate the write performance of ExtendedMoR feature. The artifacts are under extendedmorwrite1/ directory.

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, start embedded HMS:
```bash
tmux new -s hms
rm -rf metastore_db/

schematool -dbType derby -initSchema

hive --service metastore
```

On Node 4, create a configuration json file for the experiment. Example file is under extendedmorwrite1/. The cofiguration parameters are as follows:
* exp_type : With Extended MoR ("raven") or plain Iceberg ("vanilla"). 
* duration : Duration of the experiment.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* txn_per_compaction" : Compaction interval.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment for Iceberg:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.ExtendedMORWrite1 <config_path>
```

The instructions for Delta Lake are identical except for Node 4. On Node 4, HMS set up is not necessary.
To run the experiment for Delta Lake:
```bash
java -Dlogback.configurationFile=<raven_path>/experiments/logback.xml \
     -cp "<delta_lake_path>/target/all-jars/*" io.delta.kernel.defaults.ExtendedMORWrite1 \
     <config_path>
```

## ExtendedMoR Write Experiment2
Same experiment as above, except the experiment runs over a set number of iterations, rather than time. The artifacts are under extendedmorwrite2/ directory.

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, start embedded HMS:
```bash
tmux new -s hms
rm -rf metastore_db/

schematool -dbType derby -initSchema

hive --service metastore
```

On Node 4, create a configuration json file for the experiment. Example file is under extendedmorwrite2/. The cofiguration parameters are as follows:
* exp_type : With Extended MoR ("raven") or plain Iceberg ("vanilla"). 
* num_txn : Number of iterations / transactions.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* txn_per_compaction" : Compaction interval.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment for Iceberg:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.ExtendedMORWrite2 <config_path>
```

The instructions for Delta Lake are identical except for Node 4. On Node 4, HMS set up is not necessary.
To run the experiment for Delta Lake:
```bash
java -Dlogback.configurationFile=<raven_path>/experiments/logback.xml \
     -cp "<delta_lake_path>/target/all-jars/*" io.delta.kernel.defaults.ExtendedMORWrite2 \
     <config_path>
```

## ExtendedMoR Read Experiment
An experiment for testing the impact of the extended MoR on the read performance. 

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/catalogservice/config.txt
```

On Node 4, start embedded HMS:
```bash
tmux new -s hms
rm -rf metastore_db/

schematool -dbType derby -initSchema

hive --service metastore
```

On Node 4, create a configuration json file for the experiment. Example file is under extendedmorread/. The cofiguration parameters are as follows:
* exp_type : With Extended MoR ("raven") or plain Iceberg ("vanilla").
* populate : Whether to initially populate / load the table data.
* num_data_files : Number of data files to populate / load.
* num_measures : Number of measurements to take.
* selectivity : Selectivity of the scan operation (in %).
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* txn_per_compaction" : Compaction interval.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment for Iceberg:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.ExtendedMORRead <config_path>
```

The instructions for Delta Lake are identical except for Node 4. On Node 4, HMS set up is not necessary.
To run the experiment for Delta Lake:
```bash
java -Dlogback.configurationFile=<raven_path>/experiments/logback.xml \
     -cp "<delta_lake_path>/target/all-jars/*" io.delta.kernel.defaults.ExtendedMORRead \
     <config_path>
```

## Multi-Table Transactions Experiment 2
An experiment for evaluating multi-table transaction implementations for Iceberg and Delta Lake. 

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, start embedded HMS:
```bash
tmux new -s hms
rm -rf metastore_db/

schematool -dbType derby -initSchema

hive --service metastore
```

On Node 4, create a configuration json file for the experiment. Example file is under multitabletxn2/. The cofiguration parameters are as follows:
* exp_type : With Extended MoR ("raven") or plain Iceberg ("vanilla").
* num_txn : Number of iterations / transactions.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* num_tables : Number of target tables for a single multi-table insert operation. 
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment for Iceberg:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.MultiTableTxn2 <config_path>
```

The instructions for Delta Lake are identical except for Node 4. On Node 4, HMS set up is not necessary.
To run the experiment for Delta Lake:
```bash
java -Dlogback.configurationFile=<raven_path>/experiments/logback.xml \
     -cp "<delta_lake_path>/target/all-jars/*" io.delta.kernel.defaults.MultiTableTxn2 \
     <config_path>
```

## Multi-Table Transactions Experiment 3
An experiment for evaluating multi-table transaction implementation of Raven. Unlike previous expriment, this is more comprehensive, testing multiple client threads, skew etc without Iceberg and Delta Lake integrations.  

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate2.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, create a configuration json file for the experiment. Example file is under multitabletxn3/. The cofiguration parameters are as follows:
* skew : Skew parameter for controlling contention.
* refresh_ratio : Proportion of refresh operations.
* insert_ratio : Proportion of insert operations.
* compact_ratio : Proportion of compact operations.
* duration : Duration of the experiment.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.MultiTableTxn3 <config_path>
```

## GC Experiment1
An experiment for evaluating the impact of garbage collection on other metadata operations, indirectly measuring any overheads. 

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate2.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector if evaluating with gc enabled. Otherwise, skip this step.
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, create a configuration json file for the experiment. Example file is under gc1/. The cofiguration parameters are as follows:
* refresh_ratio : Proportion of refresh operations.
* insert_ratio : Proportion of insert operations.
* duration : Duration of the experiment.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_threads : Number of client threads.
* txn_per_compaction : Compaction interval.
* num_rows_per_file : Number of rows per file.
* warehouse_location : Path of the database.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.


Run the experiment:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.GC1 <config_path>
```

## GC Experiment2
An experiment for evaluating the scalability of the garbage collection.

Delete all files created as a result of any previous experiment run in S3, using AWS console.

On Node 1, run <raven_path>/experiments/metadatastore/clean.sql

On Node 2, run the following to set up the catalog service
```bash
<raven_path>/build/src/setup <raven_path>/experiments/catalogservice/config.txt <raven_path>/experiments/catalogservice/setup.erql <raven_path>/experiments/catalogservice/populate2.erql
<raven_path>/build/src/server <raven_path>/experiments/catalogservice/config.txt
```

On Node 3, run the garbage collector
```bash
<raven_path>/build/src/rungc <raven_path>/experiments/catalogservice/config.txt
```

On Node 4, create a configuration json file for the experiment. Example file is under gc2/. The cofiguration parameters are as follows:
* duration : Duration of the experiment.
* workspace_name : Name of the workspace.
* db_name : Name of the database.
* table_name : Name of the table.
* num_populators : Number of threads for populating the data & metadata,
* num_threads : Number of client threads.
* txn_per_compaction : Compaction interval.
* num_rows_per_file : Number of rows per file.
* num_expire_snapshots : Number of snapshots to expire at a time.
* num_files : Total number of data files to load / populate.
* sleep_time : Pause time between each client operation.
* warehouse_location : Path of the database.
* s3_secret : AWS secret key for S3.
* s3_key_id : AWS access key for S3.
* s3_region : AWS region for S3.
* raven_address : Host address of the catalog service (gRPC).
* exp_result_dir : Output directory for the experimental result.

Run the experiment:
```bash
java -cp "<iceberg_path>/exp/build/install/iceberg-exp/lib/*" \
    org.apache.iceberg.exp.GC2 <config_path>
```