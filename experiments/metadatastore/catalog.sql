CREATE SCHEMA IF NOT EXISTS ercat AUTHORIZATION koh;

DROP TABLE IF EXISTS ercat.Entity CASCADE;
CREATE TABLE ercat.Entity (
    ent_id INT4 PRIMARY KEY,
    ent_name VARCHAR(128) NOT NULL,
    is_abstract BOOL NOT NULL,
    is_file_list BOOL NOT NULL,
    is_root BOOL NOT NULL
);

DROP TABLE IF EXISTS ercat.Implements CASCADE;
CREATE TABLE ercat.Implements (
    src_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    dest_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    PRIMARY KEY(src_ent_id, dest_ent_id)
);

DROP TABLE IF EXISTS ercat.RelationshipName CASCADE;
CREATE TABLE ercat.RelationshipName (
    rel_name_id INT4 PRIMARY KEY,
    rel_name VARCHAR(128) NOT NULL,
    UNIQUE (rel_name)
);

-- Prob not going to check for ambiguous relationship definitions for now --
DROP TABLE IF EXISTS ercat.Relationship CASCADE;
CREATE TABLE ercat.Relationship (
    rel_id INT4 PRIMARY KEY,
    rel_name_id INT4 REFERENCES ercat.RelationshipName(rel_name_id),
    src_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    dest_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    is_referential VARCHAR(1) DEFAULT 'n'
);

-- Constraints on each realtionship type, retention period time for now --
DROP TABLE IF EXISTS ercat.RelationshipConstraints CASCADE;
CREATE TABLE ercat.RelationshipConstraints (
    constraint_name VARCHAR(128),
    rel_id INT4,
    src_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    dest_ent_id INT4 REFERENCES ercat.Entity(ent_id),
    retention_period VARCHAR(64),
    PRIMARY KEY(rel_id, src_ent_id, dest_ent_id)
);

-- TODO implement logic to handle deletion of the generator, if all references are dropped --
-- Generator for generating the next vid of a given file list object --
DROP TABLE IF EXISTS ercat.FileListVidGenerator CASCADE;
CREATE TABLE ercat.FileListVidGenerator (
    ent_id INT4 NOT NULL,
    obj_id INT8 NOT NULL,
    next_vid INT4 NOT NULL,
    PRIMARY KEY(ent_id, obj_id)
);

DROP TABLE IF EXISTS ercat.AddEdges CASCADE;
CREATE TABLE ercat.AddEdges (
    epoch_id INT8 NOT NULL,
    src_ent_id INT4 NOT NULL,
    src_vid INT4 NOT NULL,
    src_obj_id INT8 NOT NULL,
    dest_ent_id INT4 NOT NULL,
    dest_vid INT4 NOT NULL,
    dest_obj_id INT8 NOT NULL
);

DROP TABLE IF EXISTS ercat.DeleteEdges CASCADE;
CREATE TABLE ercat.DeleteEdges (
    epoch_id INT8 NOT NULL,
    delete_time TIMESTAMPTZ NOT NULL,
    src_ent_id INT4 NOT NULL,
    src_vid INT4 NOT NULL,
    src_obj_id INT8 NOT NULL,
    dest_ent_id INT4 NOT NULL,
    dest_vid INT4 NOT NULL,
    dest_obj_id INT8 NOT NULL
);

CREATE INDEX AddEdgesIdx ON ercat.AddEdges (epoch_id);

CREATE INDEX DeleteEdgesIdx ON ercat.DeleteEdges (delete_time, epoch_id);

DROP TABLE IF EXISTS ercat.EpochId CASCADE;
CREATE TABLE ercat.EpochId (
    epoch_id INT8 PRIMARY KEY
);

INSERT INTO ercat.EpochId (epoch_id)
VALUES (1);

DROP TABLE IF EXISTS ercat.CurTime CASCADE;
CREATE TABLE ercat.CurTime (
    cur_time TIMESTAMPTZ
);

INSERT INTO ercat.CurTime (cur_time)
VALUES (NOW());

-- files that have been added to any file list, duplicates indicate multiple file lists contain the file --
DROP TABLE IF EXISTS ercat.AddFiles CASCADE;
CREATE TABLE ercat.AddFiles (
    file_path VARCHAR(1024)
) PARTITION BY HASH (file_path);

-- files that have been deleted from any file list, duplicates indicate the file was deleted in multiple file lists --
DROP TABLE IF EXISTS ercat.DeleteFiles CASCADE;
CREATE TABLE ercat.DeleteFiles (
    file_path VARCHAR(1024)
) PARTITION BY HASH (file_path);

CREATE OR REPLACE FUNCTION create_hash_partitions(
    parent_table_name TEXT,
    num_partitions INT
)
RETURNS VOID AS $$
DECLARE
    i INT;
    partition_name TEXT;
    sql_command TEXT;
BEGIN
    -- Loop from 0 up to (num_partitions - 1) for the REMAINDER value
    FOR i IN 0..(num_partitions - 1)
    LOOP
        -- Construct the partition name, e.g., 'user_activity_log_0'
        partition_name := parent_table_name || '_' || i;
        
        -- Construct the SQL command for creating the partition
        sql_command := format(
            'CREATE TABLE %s PARTITION OF %s FOR VALUES WITH (MODULUS %s, REMAINDER %s);',
            partition_name,
            parent_table_name,
            num_partitions,
            i
        );

        -- Execute the dynamic SQL
        EXECUTE sql_command;
        
        RAISE NOTICE 'Created partition: %', partition_name;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT create_hash_partitions('ercat.AddFiles', 32);

SELECT create_hash_partitions('ercat.DeleteFiles', 32);

CREATE INDEX AddFilesIdx ON ercat.AddFiles (file_path);

CREATE INDEX DeleteFilesIdx ON ercat.DeleteFiles (file_path);

-- partition id compuation is hard-coded in the catalog for peformance & logistic reasons --
-- end_vid is maximum int (inf) by default --
DROP TABLE IF EXISTS ercat.Files CASCADE;
CREATE TABLE ercat.Files (
    partition_id INT4,
    ent_id INT4 NOT NULL,
    obj_id INT8 NOT NULL,
    file_path VARCHAR(1024) NOT NULL,
    format VARCHAR(64),
    file_size INT4,
    modification_time INT8,
    tag VARCHAR(128),
    start_vid INT4 NOT NULL,
    end_vid INT4 NOT NULL
) PARTITION BY LIST (partition_id);

CREATE OR REPLACE FUNCTION create_list_partitions(
    parent_table_name TEXT,
    num_partitions INT
)
RETURNS VOID AS $$
DECLARE
    i INT;
    partition_name TEXT;
    sql_command TEXT;
BEGIN
    -- Loop from 0 up to (num_partitions - 1) for the REMAINDER value
    FOR i IN 0..(num_partitions - 1)
    LOOP
        -- Construct the partition name, e.g., 'user_activity_log_0'
        partition_name := parent_table_name || '_' || i;
        
        -- Construct the SQL command for creating the partition
        sql_command := format(
            'CREATE TABLE %s PARTITION OF %s FOR VALUES IN (%s);',
            partition_name,
            parent_table_name,
            i
        );

        -- Execute the dynamic SQL
        EXECUTE sql_command;
        
        RAISE NOTICE 'Created partition: %', partition_name;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

SELECT create_list_partitions('ercat.Files', 1024);

-- TODO try out different index configs --
CREATE INDEX FilesStartVIdIdx ON ercat.Files (ent_id, obj_id, start_vid);

CREATE INDEX FilesEndVIdIdx ON ercat.Files (ent_id, obj_id, end_vid);