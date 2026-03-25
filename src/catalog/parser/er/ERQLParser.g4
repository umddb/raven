/*
ERQL grammar.
The MIT License (MIT).
Copyright (c) 2021-2023, Oleksii Kovalov (Oleksii.Kovalov@outlook.com).
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// $antlr-format alignTrailingComments true, columnLimit 150, minEmptyLines 1, maxEmptyLinesToKeep 1, reflowComments false, useTab false
// $antlr-format allowShortRulesOnASingleLine false, allowShortBlocksOnASingleLine true, alignSemicolons hanging, alignColons hanging

parser grammar ERQLParser;

options {
    tokenVocab = ERQLLexer;
    superClass = ERQLParserBase;
}

@header {#include "ERQLParserBase.h"}

root
    : stmtblock EOF
    ;

stmtblock
    : stmtmulti
    ;

stmtmulti
    : stmt? (SEMI stmt?)*
    ;

stmt
    : alterentstmt
    | alterrelstmt
    //| alterobjectschemastmt
    //| altertypestmt
    //| altercompositetypestmt
    //| callstmt
    //| constraintssetstmt
    //| createasstmt
    //| creatematviewstmt
    //| createseqstmt
    | createentstmt
    | createrelstmt
    //| createstatsstmt
    //| createtrigstmt
    //| createeventtrigstmt
    //| definestmt
    | deleterootstmt
    | deleterelstmt
    | deletefilestmt
    //| dropstmt
    //| indexstmt
    | insertrootstmt
    | insertrelstmt
    | insertentstmt
    | insertfilestmt
    //| preparestmt
    | selectstmt
    | selectfiledeltastmt
    | selectfilesnapshotstmt
    | transactionstmt
    //| updatestmt
    //| viewstmt
    ;


with_
    : WITH
    //| WITH_LA
    ;

iso_level
    : READ (UNCOMMITTED | COMMITTED)
    | REPEATABLE READ
    | SERIALIZABLE
    ;

alterentstmt
    : ALTER ENTITY (IF_P EXISTS)? qualified_name alter_ent_cmd
    ;

alter_ent_cmd
    : SET entconstraintelem
    ;

entconstraintelem
    : ROOT
    ;

alterrelstmt
    : ALTER RELATIONSHIP (IF_P EXISTS)? qualified_name OPEN_PAREN qualified_name COMMA qualified_name CLOSE_PAREN SET relconstraintelem
    ;

relconstraintelem
    : qualified_name REFERENCES qualified_name
    | RETENTION INTERVAL StringConstant
    ;


drop_behavior_
    : CASCADE
    | RESTRICT
    
    ;

collate_clause_
    : COLLATE any_name
    
    ;

alter_using
    : USING a_expr
    
    ;

replica_identity
    : NOTHING
    | FULL
    | DEFAULT
    | USING INDEX name
    ;

reloptions
    : OPEN_PAREN reloption_list CLOSE_PAREN
    ;

reloptions_
    : WITH reloptions
    
    ;

reloption_list
    : reloption_elem (COMMA reloption_elem)*
    ;

reloption_elem
    : colLabel (EQUAL def_arg | DOT colLabel (EQUAL def_arg)?)?
    ;

alter_identity_column_option_list
    : alter_identity_column_option+
    ;

alter_identity_column_option
    : RESTART (with_? numericonly)?
    | SET (seqoptelem | GENERATED generated_when)
    ;

hash_partbound_elem
    : nonreservedword iconst
    ;

hash_partbound
    : hash_partbound_elem (COMMA hash_partbound_elem)*
    ;

alter_type_cmd
    : ADD_P ATTRIBUTE tablefuncelement drop_behavior_?
    | DROP ATTRIBUTE (IF_P EXISTS)? colid drop_behavior_?
    | ALTER ATTRIBUTE colid set_data_? TYPE_P typename collate_clause_? drop_behavior_?
    ;

createentstmt
    : CREATE (ABSTRACT | FILELIST | ROOT)? ENTITY (IF_P NOT EXISTS)? qualified_name
        OPEN_PAREN opttableelementlist? CLOSE_PAREN optimplement?
    ;

createrelstmt
    : CREATE RELATIONSHIP (IF_P NOT EXISTS)? qualified_name 
        OPEN_PAREN qualified_name COMMA qualified_name CLOSE_PAREN
    ;

opttemp
    : TEMPORARY
    | TEMP
    | LOCAL (TEMPORARY | TEMP)
    | GLOBAL (TEMPORARY | TEMP)
    | UNLOGGED
    
    ;

opttableelementlist
    : tableelementlist
    
    ;

opttypedtableelementlist
    : OPEN_PAREN typedtableelementlist CLOSE_PAREN
    
    ;

tableelementlist
    : tableelement (COMMA tableelement)*
    ;

typedtableelementlist
    : typedtableelement (COMMA typedtableelement)*
    ;

tableelement
    : tableconstraint
    | tablelikeclause
    | columnDef
    ;

typedtableelement
    : columnOptions
    | tableconstraint
    ;

columnDef
    : colid typename create_generic_options? colquallist
    ;

columnOptions
    : colid (WITH OPTIONS)? colquallist
    ;

colquallist
    : colconstraint*
    ;

colconstraint
    : CONSTRAINT name colconstraintelem
    | colconstraintelem
    | constraintattr
    | COLLATE any_name
    ;

colconstraintelem
    : NOT NULL_P
    | NULL_P
    | UNIQUE definition_? optconstablespace?
    | PRIMARY KEY definition_? optconstablespace?
    | DEFAULT b_expr
    | GENERATED generated_when AS (
        IDENTITY_P optparenthesizedseqoptlist?
        | OPEN_PAREN a_expr CLOSE_PAREN STORED
    )
    | REFERENCES qualified_name column_list_? key_match? key_actions?
    ;

generated_when
    : ALWAYS
    | BY DEFAULT
    ;

constraintattr
    : DEFERRABLE
    | NOT DEFERRABLE
    | INITIALLY (DEFERRED | IMMEDIATE)
    ;

tablelikeclause
    : LIKE qualified_name tablelikeoptionlist
    ;

tablelikeoptionlist
    : ((INCLUDING | EXCLUDING) tablelikeoption)*
    ;

tablelikeoption
    : COMMENTS
    | CONSTRAINTS
    | DEFAULTS
    | IDENTITY_P
    | GENERATED
    | INDEXES
    | STATISTICS
    | STORAGE
    | ALL
    ;

tableconstraint
    : CONSTRAINT name constraintelem
    | constraintelem
    ;

constraintelem
    : UNIQUE (
        OPEN_PAREN columnlist CLOSE_PAREN c_include_? definition_? optconstablespace? constraintattributespec
        | existingindex constraintattributespec
    )
    | PRIMARY KEY (
        OPEN_PAREN columnlist CLOSE_PAREN c_include_? definition_? optconstablespace? constraintattributespec
        | existingindex constraintattributespec
    )
    | EXCLUDE access_method_clause? OPEN_PAREN exclusionconstraintlist CLOSE_PAREN c_include_? definition_? optconstablespace? exclusionwhereclause?
        constraintattributespec
    | FOREIGN KEY OPEN_PAREN columnlist CLOSE_PAREN REFERENCES qualified_name column_list_? key_match? key_actions? constraintattributespec
    ;

no_inherit_
    : NO INHERIT
    
    ;

column_list_
    : OPEN_PAREN columnlist CLOSE_PAREN
    
    ;

columnlist
    : columnElem (COMMA columnElem)*
    ;

columnElem
    : colid
    ;

c_include_
    : INCLUDE OPEN_PAREN columnlist CLOSE_PAREN
    
    ;

key_match
    : MATCH (FULL | PARTIAL | SIMPLE)
    
    ;

exclusionconstraintlist
    : exclusionconstraintelem (COMMA exclusionconstraintelem)*
    ;

exclusionconstraintelem
    : index_elem WITH (any_operator | OPERATOR OPEN_PAREN any_operator CLOSE_PAREN)
    ;

exclusionwhereclause
    : WHERE OPEN_PAREN a_expr CLOSE_PAREN
    
    ;

key_actions
    : key_update
    | key_delete
    | key_update key_delete
    | key_delete key_update
    
    ;

key_update
    : ON UPDATE key_action
    ;

key_delete
    : ON DELETE_P key_action
    ;

key_action
    : NO ACTION
    | RESTRICT
    | CASCADE
    | SET (NULL_P | DEFAULT)
    ;

optimplement
    : IMPLEMENTS qualified_name_list
    
    ;

optinherit
    : INHERITS OPEN_PAREN qualified_name_list CLOSE_PAREN
    
    ;

optpartitionspec
    : partitionspec
    
    ;

partitionspec
    : PARTITION BY colid OPEN_PAREN part_params CLOSE_PAREN
    ;

part_params
    : part_elem (COMMA part_elem)*
    ;

part_elem
    : colid collate_? class_?
    | func_expr_windowless collate_? class_?
    | OPEN_PAREN a_expr CLOSE_PAREN collate_? class_?
    ;

table_access_method_clause
    : USING name
    
    ;

optwith
    : WITH reloptions
    | WITHOUT OIDS
    
    ;

oncommitoption
    : ON COMMIT (DROP | DELETE_P ROWS | PRESERVE ROWS)
    
    ;

opttablespace
    : TABLESPACE name
    
    ;

optconstablespace
    : USING INDEX TABLESPACE name
    
    ;

existingindex
    : USING INDEX name
    ;

createstatsstmt
    : CREATE STATISTICS (IF_P NOT EXISTS)? any_name name_list_? ON expr_list FROM from_list
    ;

create_as_target
    : qualified_name column_list_? table_access_method_clause? optwith? oncommitoption? opttablespace?
    ;

with_data_
    : WITH (DATA_P | NO DATA_P)
    
    ;

creatematviewstmt
    : CREATE optnolog? MATERIALIZED VIEW (IF_P NOT EXISTS)? create_mv_target AS selectstmt with_data_?
    ;

create_mv_target
    : qualified_name column_list_? table_access_method_clause? reloptions_? opttablespace?
    ;

optnolog
    : UNLOGGED
    
    ;

createseqstmt
    : CREATE opttemp? SEQUENCE (IF_P NOT EXISTS)? qualified_name optseqoptlist?
    ;

optseqoptlist
    : seqoptlist
    
    ;

optparenthesizedseqoptlist
    : OPEN_PAREN seqoptlist CLOSE_PAREN
    
    ;

seqoptlist
    : seqoptelem+
    ;

seqoptelem
    : AS simpletypename
    | CACHE numericonly
    | CYCLE
    | INCREMENT by_? numericonly
    | MAXVALUE numericonly
    | MINVALUE numericonly
    | NO (MAXVALUE | MINVALUE | CYCLE)
    | OWNED BY any_name
    | SEQUENCE NAME_P any_name
    | START with_? numericonly
    | RESTART with_? numericonly?
    ;

by_
    : BY
    
    ;

numericonly
    : fconst
    | PLUS fconst
    | MINUS fconst
    | signediconst
    ;

numericonly_list
    : numericonly (COMMA numericonly)*
    ;

procedural_
    : PROCEDURAL
    
    ;

create_generic_options
    : OPTIONS OPEN_PAREN generic_option_list CLOSE_PAREN
    
    ;

generic_option_list
    : generic_option_elem (COMMA generic_option_elem)*
    ;

alter_generic_options
    : OPTIONS OPEN_PAREN alter_generic_option_list CLOSE_PAREN
    ;

alter_generic_option_list
    : alter_generic_option_elem (COMMA alter_generic_option_elem)*
    ;

alter_generic_option_elem
    : generic_option_elem
    | SET generic_option_elem
    | ADD_P generic_option_elem
    | DROP generic_option_name
    ;

generic_option_elem
    : generic_option_name generic_option_arg
    ;

generic_option_name
    : colLabel
    ;

generic_option_arg
    : sconst
    ;

type_
    : TYPE_P sconst
    
    ;

createtrigstmt
    : CREATE TRIGGER name triggeractiontime triggerevents ON qualified_name triggerreferencing? triggerforspec? triggerwhen? EXECUTE
        function_or_procedure func_name OPEN_PAREN triggerfuncargs CLOSE_PAREN
    | CREATE CONSTRAINT TRIGGER name AFTER triggerevents ON qualified_name optconstrfromtable? constraintattributespec FOR EACH ROW triggerwhen? EXECUTE
        function_or_procedure func_name OPEN_PAREN triggerfuncargs CLOSE_PAREN
    ;

triggeractiontime
    : BEFORE
    | AFTER
    | INSTEAD OF
    ;

triggerevents
    : triggeroneevent (OR triggeroneevent)*
    ;

triggeroneevent
    : INSERT
    | DELETE_P
    | UPDATE
    | UPDATE OF columnlist
    | TRUNCATE
    ;

triggerreferencing
    : REFERENCING triggertransitions
    
    ;

triggertransitions
    : triggertransition+
    ;

triggertransition
    : transitionoldornew transitionrowortable as_? transitionrelname
    ;

transitionoldornew
    : NEW
    | OLD
    ;

transitionrowortable
    : ROW
    ;

transitionrelname
    : colid
    ;

triggerforspec
    : FOR triggerforopteach? triggerfortype
    
    ;

triggerforopteach
    : EACH
    
    ;

triggerfortype
    : ROW
    | STATEMENT
    ;

triggerwhen
    : WHEN OPEN_PAREN a_expr CLOSE_PAREN
    
    ;

function_or_procedure
    : FUNCTION
    | PROCEDURE
    ;

triggerfuncargs
    : (triggerfuncarg |) (COMMA triggerfuncarg)*
    ;

triggerfuncarg
    : iconst
    | fconst
    | sconst
    | colLabel
    ;

optconstrfromtable
    : FROM qualified_name
    
    ;

constraintattributespec
    : constraintattributeElem*
    ;

constraintattributeElem
    : NOT DEFERRABLE
    | DEFERRABLE
    | INITIALLY IMMEDIATE
    | INITIALLY DEFERRED
    | NOT VALID
    | NO INHERIT
    ;

createeventtrigstmt
    : CREATE EVENT TRIGGER name ON colLabel EXECUTE function_or_procedure func_name OPEN_PAREN CLOSE_PAREN
    | CREATE EVENT TRIGGER name ON colLabel WHEN event_trigger_when_list EXECUTE function_or_procedure func_name OPEN_PAREN CLOSE_PAREN
    ;

event_trigger_when_list
    : event_trigger_when_item (AND event_trigger_when_item)*
    ;

event_trigger_when_item
    : colid IN_P OPEN_PAREN event_trigger_value_list CLOSE_PAREN
    ;

event_trigger_value_list
    : sconst (COMMA sconst)*
    ;

definition
    : OPEN_PAREN def_list CLOSE_PAREN
    ;

def_list
    : def_elem (COMMA def_elem)*
    ;

def_elem
    : colLabel (EQUAL def_arg)?
    ;

def_arg
    : func_type
    | reserved_keyword
    | qual_all_op
    | numericonly
    | sconst
    | NONE
    ;

old_aggr_definition
    : OPEN_PAREN old_aggr_list CLOSE_PAREN
    ;

old_aggr_list
    : old_aggr_elem (COMMA old_aggr_elem)*
    ;

old_aggr_elem
    : identifier EQUAL def_arg
    ;

enum_val_list_
    : enum_val_list
    
    ;

enum_val_list
    : sconst (COMMA sconst)*
    ;

if_not_exists_
    : IF_P NOT EXISTS
    ;

drop_type_name
    : ACCESS METHOD
    | EVENT TRIGGER
    | EXTENSION
    | FOREIGN DATA_P WRAPPER
    | procedural_? LANGUAGE
    | PUBLICATION
    | SCHEMA
    | SERVER
    ;

any_name
    : colid attrs?
    ;

attrs
    : (DOT attr_name)+
    ;

type_name_list
    : typename (COMMA typename)*
    ;

restart_seqs_
    : CONTINUE_P IDENTITY_P
    | RESTART IDENTITY_P
    
    ;

from_in
    : FROM
    | IN_P
    ;

from_in_
    : from_in
    
    ;

//create index

indexstmt
    : CREATE unique_? INDEX concurrently_? index_name_? ON relation_expr access_method_clause? OPEN_PAREN index_params CLOSE_PAREN include_?
        reloptions_? opttablespace? where_clause?
    | CREATE unique_? INDEX concurrently_? IF_P NOT EXISTS name ON relation_expr access_method_clause? OPEN_PAREN index_params CLOSE_PAREN
        include_? reloptions_? opttablespace? where_clause?
    ;

unique_
    : UNIQUE
    
    ;

concurrently_
    : CONCURRENTLY
    
    ;

index_name_
    : name
    
    ;

access_method_clause
    : USING name
    
    ;

index_params
    : index_elem (COMMA index_elem)*
    ;

index_elem_options
    : collate_? class_? asc_desc_? nulls_order_?
    | collate_? any_name reloptions asc_desc_? nulls_order_?
    ;

index_elem
    : colid index_elem_options
    | func_expr_windowless index_elem_options
    | OPEN_PAREN a_expr CLOSE_PAREN index_elem_options
    ;

include_
    : INCLUDE OPEN_PAREN index_including_params CLOSE_PAREN
    
    ;

index_including_params
    : index_elem (COMMA index_elem)*
    ;

collate_
    : COLLATE any_name
    
    ;

class_
    : any_name
    
    ;

asc_desc_
    : ASC
    | DESC
    
    ;

//TOD NULLS_LA was used

nulls_order_
    : NULLS_P FIRST_P
    | NULLS_P LAST_P
    
    ;

or_replace_
    : OR REPLACE
    
    ;

func_args
    : OPEN_PAREN func_args_list? CLOSE_PAREN
    ;

func_args_list
    : func_arg (COMMA func_arg)*
    ;

function_with_argtypes_list
    : function_with_argtypes (COMMA function_with_argtypes)*
    ;

function_with_argtypes
    : func_name func_args
    | type_func_name_keyword
    | colid indirection?
    ;

func_args_with_defaults
    : OPEN_PAREN func_args_with_defaults_list? CLOSE_PAREN
    ;

func_args_with_defaults_list
    : func_arg_with_default (COMMA func_arg_with_default)*
    ;

func_arg
    : arg_class param_name? func_type
    | param_name arg_class? func_type
    | func_type
    ;

arg_class
    : IN_P OUT_P?
    | OUT_P
    | INOUT
    | VARIADIC
    ;

param_name
    : type_function_name
    ;

func_return
    : func_type
    ;

func_type
    : typename
    | SETOF? type_function_name attrs PERCENT TYPE_P
    ;

func_arg_with_default
    : func_arg ((DEFAULT | EQUAL) a_expr)?
    ;

aggr_arg
    : func_arg
    ;

aggr_args
    : OPEN_PAREN (
        STAR
        | aggr_args_list
        | ORDER BY aggr_args_list
        | aggr_args_list ORDER BY aggr_args_list
    ) CLOSE_PAREN
    ;

aggr_args_list
    : aggr_arg (COMMA aggr_arg)*
    ;

aggregate_with_argtypes
    : func_name aggr_args
    ;

//https://www.postgresql.org/docs/9.1/sql-createfunction.html

//    | AS 'definition'

//    | AS 'obj_file', 'link_symbol'

func_as
    :
    /* |AS 'definition'*/ def = sconst
    /*| AS 'obj_file', 'link_symbol'*/
    | sconst COMMA sconst
    ;

definition_
    : WITH definition
    
    ;

table_func_column
    : param_name func_type
    ;

table_func_column_list
    : table_func_column (COMMA table_func_column)*
    ;

oper_argtypes
    : OPEN_PAREN typename CLOSE_PAREN
    | OPEN_PAREN typename COMMA typename CLOSE_PAREN
    | OPEN_PAREN NONE COMMA typename CLOSE_PAREN
    | OPEN_PAREN typename COMMA NONE CLOSE_PAREN
    ;

any_operator
    : (colid DOT)* all_op
    ;

operator_with_argtypes_list
    : operator_with_argtypes (COMMA operator_with_argtypes)*
    ;

operator_with_argtypes
    : any_operator oper_argtypes
    ;

column_
    : COLUMN
    
    ;

set_data_
    : SET DATA_P
    
    ;

operator_def_list
    : operator_def_elem (COMMA operator_def_elem)*
    ;

operator_def_elem
    : colLabel EQUAL NONE
    | colLabel EQUAL operator_def_arg
    ;

operator_def_arg
    : func_type
    | reserved_keyword
    | qual_all_op
    | numericonly
    | sconst
    ;

altertypestmt
    : ALTER TYPE_P any_name SET OPEN_PAREN operator_def_list CLOSE_PAREN
    ;

event
    : SELECT
    | UPDATE
    | DELETE_P
    | INSERT
    ;

viewstmt
    : CREATE (OR REPLACE)? opttemp? (
        VIEW qualified_name column_list_? reloptions_?
        | RECURSIVE VIEW qualified_name OPEN_PAREN columnlist CLOSE_PAREN reloptions_?
    ) AS selectstmt
    ;

as_
    : AS
    
    ;

name_list_
    : OPEN_PAREN name_list CLOSE_PAREN
    
    ;

preparestmt
    : PREPARE name prep_type_clause? AS preparablestmt
    ;

prep_type_clause
    : OPEN_PAREN type_list CLOSE_PAREN
    
    ;

preparablestmt
    : selectstmt
    | insertrelstmt
    | insertentstmt
    | updatestmt
    | deleterelstmt
    ;

insertrootstmt
    : INSERT INTO ROOT ENTITY qualified_name 
        insert_rest on_conflict_?
    ;

insertentstmt
    : with_clause? INSERT INTO ENTITY qualified_name 
        insert_rest on_conflict_? returning_clause?
    ;

insertrelstmt
    : with_clause? INSERT INTO RELATIONSHIP qualified_name OPEN_PAREN qualified_name COMMA qualified_name CLOSE_PAREN
        insert_rest on_conflict_?
    ;

insertfilestmt
    : PREPARE? INSERT INTO FILELIST qualified_name OPEN_PAREN Integral COMMA Integral CLOSE_PAREN
      OPEN_PAREN insert_column_list CLOSE_PAREN values_clause
    ;

insert_target
    : qualified_name (AS colid)?
    ;

insert_rest
    : selectstmt
    | OVERRIDING override_kind VALUE_P selectstmt
    | OPEN_PAREN insert_column_list CLOSE_PAREN (OVERRIDING override_kind VALUE_P)? selectstmt
    | DEFAULT VALUES
    ;

override_kind
    : USER
    | SYSTEM_P
    ;

insert_column_list
    : insert_column_item (COMMA insert_column_item)*
    ;

insert_column_item
    : colid opt_indirection
    ;

on_conflict_
    : ON CONFLICT conf_expr_? DO (UPDATE SET set_clause_list where_clause? | NOTHING)
    
    ;

conf_expr_
    : OPEN_PAREN index_params CLOSE_PAREN where_clause?
    | ON CONSTRAINT name
    
    ;

returning_clause
    : RETURNING target_list1
    
    ;

deleterootstmt
    : DELETE_P FROM ROOT ENTITY qualified_name where_clause?
    ;

deleterelstmt
    : with_clause? DELETE_P FROM RELATIONSHIP qualified_name OPEN_PAREN qualified_name COMMA qualified_name CLOSE_PAREN 
        where_clause? overridingret?
    ;

overridingret
    : OVERRIDING RETENTION INTERVAL StringConstant
    ;

deletefilestmt
    : PREPARE? DELETE_P FROM FILELIST qualified_name OPEN_PAREN Integral COMMA Integral CLOSE_PAREN values_clause
    ;


nowait_or_skip_
    : NOWAIT
    | SKIP_P LOCKED
    
    ;

updatestmt
    : with_clause? UPDATE relation_expr_opt_alias SET set_clause_list from_clause? where_or_current_clause? returning_clause?
    ;

set_clause_list
    : set_clause (COMMA set_clause)*
    ;

set_clause
    : set_target EQUAL a_expr
    | OPEN_PAREN set_target_list CLOSE_PAREN EQUAL a_expr
    ;

set_target
    : colid opt_indirection
    ;

set_target_list
    : set_target (COMMA set_target)*
    ;

cursor_name
    : name
    ;

/*
TODO: why select_with_parens alternative is needed at all?
i guess it because original byson grammar can choose selectstmt(2)->select_with_parens on only OPEN_PARENT/SELECT kewords at the begining of statement;
(select * from tab);
parse can go through selectstmt( )->select_no_parens(1)->select_clause(2)->select_with_parens(1)->select_no_parens(1)->select_clause(1)->simple_select
instead of           selectstmt(1)->select_no_parens(1)->select_clause(2)->select_with_parens(1)->select_no_parens(1)->select_clause(1)->simple_select
all standard tests passed on both variants
*/

selectstmt
    : select_no_parens
    | select_with_parens
    ;

select_with_parens
    : OPEN_PAREN select_no_parens CLOSE_PAREN
    | OPEN_PAREN select_with_parens CLOSE_PAREN
    ;

select_no_parens
    : select_clause sort_clause_? (
        for_locking_clause select_limit_?
        | select_limit for_locking_clause_?
    )?
    | with_clause select_clause sort_clause_? (
        for_locking_clause select_limit_?
        | select_limit for_locking_clause_?
    )?
    ;


select_clause
    : simple_select_intersect ((UNION | EXCEPT) all_or_distinct? simple_select_intersect)*
    ;

simple_select_intersect
    : simple_select_pramary (INTERSECT all_or_distinct? simple_select_pramary)*
    ;

simple_select_pramary
    : (
        SELECT
	( target_list_ from_clause2 where_clause? )
    )
    | values_clause
    | select_with_parens
    ;

selectfiledeltastmt
    : (
        SELECT
	( target_list1 FROM FILELIST DELTA qualified_name OPEN_PAREN Integral COMMA Integral COMMA Integral CLOSE_PAREN where_clause? )
    )
    ;

selectfilesnapshotstmt
    : (
        SELECT
	( target_list1 FROM FILELIST SNAPSHOT qualified_name OPEN_PAREN Integral COMMA Integral CLOSE_PAREN where_clause? )
    )
    ;

with_clause
    : WITH common_table_expr (COMMA common_table_expr)*
    ;

common_table_expr
    : name name_list_? AS materialized_? OPEN_PAREN preparablestmt CLOSE_PAREN
    ;

materialized_
    : MATERIALIZED
    | NOT MATERIALIZED    
    ;

all_or_distinct
    : ALL
    | DISTINCT
    
    ;

distinct_clause
    : DISTINCT (ON OPEN_PAREN expr_list CLOSE_PAREN)?
    ;

all_clause_
    : ALL
    
    ;

sort_clause_
    : sort_clause
    
    ;

sort_clause
    : ORDER BY sortby_list
    ;

sortby_list
    : sortby (COMMA sortby)*
    ;

sortby
    : a_expr (USING qual_all_op | asc_desc_?) nulls_order_?
    ;

select_limit
    : limit_clause offset_clause?
    | offset_clause limit_clause?
    ;

select_limit_
    : select_limit
    
    ;

limit_clause
    : LIMIT select_limit_value (COMMA select_offset_value)?
    | FETCH first_or_next (
        select_fetch_first_value row_or_rows (ONLY | WITH TIES)
        | row_or_rows (ONLY | WITH TIES)
    )
    ;

offset_clause
    : OFFSET (select_offset_value | select_fetch_first_value row_or_rows)
    ;

select_limit_value
    : a_expr
    | ALL
    ;

select_offset_value
    : a_expr
    ;

select_fetch_first_value
    : c_expr
    | PLUS i_or_f_const
    | MINUS i_or_f_const
    ;

transactionstmt
    : BEGIN_P TRANSACTION transaction_mode_list_or_empty?
    | START TRANSACTION transaction_mode_list_or_empty?
    | COMMIT
    ;

transaction_mode_item
    : ISOLATION LEVEL iso_level
    | READ ONLY
    | READ WRITE
    | DEFERRABLE
    | NOT DEFERRABLE
    ;

transaction_mode_list
    : transaction_mode_item (COMMA? transaction_mode_item)*
    ;

transaction_mode_list_or_empty
    : transaction_mode_list
    ;


i_or_f_const
    : iconst
    | fconst
    ;

row_or_rows
    : ROW
    | ROWS
    ;

first_or_next
    : FIRST_P
    | NEXT
    ;

group_clause
    : GROUP_P BY group_by_list
    
    ;

group_by_list
    : group_by_item (COMMA group_by_item)*
    ;

group_by_item
    : empty_grouping_set
    | a_expr
    ;

empty_grouping_set
    : OPEN_PAREN CLOSE_PAREN
    ;

having_clause
    : HAVING a_expr
    
    ;

for_locking_clause
    : for_locking_items
    | FOR READ ONLY
    ;

for_locking_clause_
    : for_locking_clause
    
    ;

for_locking_items
    : for_locking_item+
    ;

for_locking_item
    : for_locking_strength locked_rels_list? nowait_or_skip_?
    ;

for_locking_strength
    : FOR ((NO KEY)? UPDATE | KEY? SHARE)
    ;

locked_rels_list
    : OF qualified_name_list
    
    ;

values_clause
    : VALUES OPEN_PAREN expr_list CLOSE_PAREN (COMMA OPEN_PAREN expr_list CLOSE_PAREN)*
    ;

from_clause
    : FROM from_list
    ;

from_clause2
    : FROM from_list2
    ;

from_list
    : table_ref (COMMA table_ref)*
    ;

from_list2
    : er_expr (COMMA er_expr)*
    ;

table_ref
    : (
        relation_expr alias_clause?
        | select_with_parens alias_clause?
        | OPEN_PAREN table_ref CLOSE_PAREN alias_clause?
        )
    ;

alias_clause
    : AS? colid (OPEN_PAREN name_list CLOSE_PAREN)?
    ;

relation_expr
    : qualified_name STAR?
    | ONLY (qualified_name | OPEN_PAREN qualified_name CLOSE_PAREN)
    ;

er_expr
    : ent_decl
    | rel_app
    ;

rel_app
    : qualified_name (OPEN_PAREN ( ent_decl | colid ) COMMA ( ent_decl | colid ) CLOSE_PAREN)
    ;

ent_decl
    : qualified_name colid
    ;

relation_expr_list
    : relation_expr (COMMA relation_expr)*
    ;

relation_expr_opt_alias
    : qualified_name (OPEN_PAREN qualified_name COMMA qualified_name CLOSE_PAREN)? (AS? colid)?
    ;

//TODO WITH_LA was used

where_clause
    : WHERE a_expr
    
    ;

where_or_current_clause
    : WHERE (CURRENT_P OF cursor_name | a_expr)
    
    ;

opttablefuncelementlist
    : tablefuncelementlist
    
    ;

tablefuncelementlist
    : tablefuncelement (COMMA tablefuncelement)*
    ;

tablefuncelement
    : colid typename collate_clause_?
    ;

xmltable
    : XMLTABLE OPEN_PAREN (
        c_expr xmlexists_argument COLUMNS xmltable_column_list
        | XMLNAMESPACES OPEN_PAREN xml_namespace_list CLOSE_PAREN COMMA c_expr xmlexists_argument COLUMNS xmltable_column_list
    ) CLOSE_PAREN
    ;

xmltable_column_list
    : xmltable_column_el (COMMA xmltable_column_el)*
    ;

xmltable_column_el
    : colid (typename xmltable_column_option_list? | FOR ORDINALITY)
    ;

xmltable_column_option_list
    : xmltable_column_option_el+
    ;

xmltable_column_option_el
    : DEFAULT a_expr
    | identifier a_expr
    | NOT NULL_P
    | NULL_P
    ;

xml_namespace_list
    : xml_namespace_el (COMMA xml_namespace_el)*
    ;

xml_namespace_el
    : b_expr AS colLabel
    | DEFAULT b_expr
    ;

typename
    : SETOF? simpletypename
	( opt_array_bounds
	| ARRAY (OPEN_BRACKET iconst CLOSE_BRACKET)?
	)
    ;

opt_array_bounds
    : (OPEN_BRACKET iconst? CLOSE_BRACKET)*
    ;

simpletypename
    : generictype
    | numeric
    | bit
    | character
    | constdatetime
    | constinterval (interval_? | OPEN_PAREN iconst CLOSE_PAREN)
    | jsonType
    ;

consttypename
    : numeric
    | constbit
    | constcharacter
    | constdatetime
    | jsonType
    ;

generictype
    : type_function_name attrs? type_modifiers_?
    ;

type_modifiers_
    : OPEN_PAREN expr_list CLOSE_PAREN
    
    ;

numeric
    : INT_P
    | INTEGER
    | SMALLINT
    | BIGINT
    | REAL
    | FLOAT_P float_?
    | DOUBLE_P PRECISION
    | DECIMAL_P type_modifiers_?
    | DEC type_modifiers_?
    | NUMERIC type_modifiers_?
    | BOOLEAN_P
    ;

float_
    : OPEN_PAREN iconst CLOSE_PAREN
    
    ;

//todo: merge alts

bit
    : bitwithlength
    | bitwithoutlength
    ;

constbit
    : bitwithlength
    | bitwithoutlength
    ;

bitwithlength
    : BIT varying_? OPEN_PAREN expr_list CLOSE_PAREN
    ;

bitwithoutlength
    : BIT varying_?
    ;

character
    : character_c (OPEN_PAREN iconst CLOSE_PAREN)?
    ;

constcharacter
    : character_c (OPEN_PAREN iconst CLOSE_PAREN)?
    ;

character_c
    : (CHARACTER | CHAR_P | NCHAR) varying_?
    | VARCHAR
    | NATIONAL (CHARACTER | CHAR_P) varying_?
    ;

varying_
    : VARYING
    
    ;

constdatetime
    : (TIMESTAMP | TIME) (OPEN_PAREN iconst CLOSE_PAREN)? timezone_?
    ;

constinterval
    : INTERVAL
    ;

//TODO with_la was used

timezone_
    : WITH TIME ZONE
    | WITHOUT TIME ZONE
    
    ;

interval_
    : YEAR_P
    | MONTH_P
    | DAY_P
    | HOUR_P
    | MINUTE_P
    | interval_second
    | YEAR_P TO MONTH_P
    | DAY_P TO (HOUR_P | MINUTE_P | interval_second)
    | HOUR_P TO (MINUTE_P | interval_second)
    | MINUTE_P TO interval_second
    
    ;

interval_second
    : SECOND_P (OPEN_PAREN iconst CLOSE_PAREN)?
    ;

jsonType
    : JSON
    ;

escape_
    : ESCAPE a_expr
    
    ;

//precendence accroding to Table 4.2. Operator Precedence (highest to lowest)

//https://www.postgresql.org/docs/12/sql-syntax-lexical.html#SQL-PRECEDENCE

/*
original version of a_expr, for info
 a_expr: c_expr
        //::	left	ERQL-style typecast
       | a_expr TYPECAST typename -- 1
       | a_expr COLLATE any_name -- 2
       | a_expr AT TIME ZONE a_expr-- 3
       //right	unary plus, unary minus
       | (PLUS| MINUS) a_expr -- 4
        //left	exponentiation
       | a_expr CARET a_expr -- 5
        //left	multiplication, division, modulo
       | a_expr (STAR | SLASH | PERCENT) a_expr -- 6
        //left	addition, subtraction
       | a_expr (PLUS | MINUS) a_expr -- 7
        //left	all other native and user-defined operators
       | a_expr qual_op a_expr -- 8
       | qual_op a_expr -- 9
        //range containment, set membership, string matching BETWEEN IN LIKE ILIKE SIMILAR
       | a_expr NOT? (LIKE|ILIKE|SIMILAR TO|(BETWEEN SYMMETRIC?)) a_expr opt_escape -- 10
        //< > = <= >= <>	 	comparison operators
       | a_expr (LT | GT | EQUAL | LESS_EQUALS | GREATER_EQUALS | NOT_EQUALS) a_expr -- 11
       //IS ISNULL NOTNULL	 	IS TRUE, IS FALSE, IS NULL, IS DISTINCT FROM, etc
       | a_expr IS NOT?
            (
                NULL_P
                |TRUE_P
                |FALSE_P
                |UNKNOWN
                |DISTINCT FROM a_expr
                |OF OPEN_PAREN type_list CLOSE_PAREN
                |DOCUMENT_P
                |unicode_normal_form? NORMALIZED
            ) -- 12
       | a_expr (ISNULL|NOTNULL) -- 13
       | row OVERLAPS row -- 14
       //NOT	right	logical negation
       | NOT a_expr -- 15
        //AND	left	logical conjunction
       | a_expr AND a_expr -- 16
        //OR	left	logical disjunction
       | a_expr OR a_expr -- 17
       | a_expr (LESS_LESS|GREATER_GREATER) a_expr -- 18
       | a_expr qual_op -- 19
       | a_expr NOT? IN_P in_expr -- 20
       | a_expr subquery_Op sub_type (select_with_parens|OPEN_PAREN a_expr CLOSE_PAREN) -- 21
       | UNIQUE select_with_parens -- 22
       | DEFAULT -- 23
;
*/

a_expr
    : a_expr_qual
    ;

/*23*/

/*moved to c_expr*/

/*22*/

/*moved to c_expr*/

/*19*/

a_expr_qual
    : a_expr_lessless ({this->OnlyAcceptableOps()}? qual_op | )
    ;

/*18*/

a_expr_lessless
    : a_expr_or ((LESS_LESS | GREATER_GREATER) a_expr_or)*
    ;

/*17*/

a_expr_or
    : a_expr_and (OR a_expr_and)*
    ;

/*16*/

a_expr_and
    : a_expr_between (AND a_expr_between)*
    ;

/*21*/

a_expr_between
    : a_expr_in (NOT? BETWEEN SYMMETRIC? a_expr_in AND a_expr_in)?
    ;

/*20*/

a_expr_in
    : a_expr_unary_not (NOT? IN_P in_expr)?
    ;

/*15*/

a_expr_unary_not
    : NOT? a_expr_isnull
    ;

/*14*/

/*moved to c_expr*/

/*13*/

a_expr_isnull
    : a_expr_is_not (ISNULL | NOTNULL)?
    ;

/*12*/

a_expr_is_not
    : a_expr_compare (
        IS NOT? (
            NULL_P
            | TRUE_P
            | FALSE_P
            | UNKNOWN
            | DISTINCT FROM a_expr
            | OF OPEN_PAREN type_list CLOSE_PAREN
            | DOCUMENT_P
            | unicode_normal_form? NORMALIZED
        )
    )?
    ;

/*11*/

a_expr_compare
    : a_expr_like (
        (LT | GT | EQUAL | LESS_EQUALS | GREATER_EQUALS | NOT_EQUALS) a_expr_like
        | subquery_Op sub_type (select_with_parens | OPEN_PAREN a_expr CLOSE_PAREN) /*21*/
    )?
    ;

/*10*/

a_expr_like
    : a_expr_qual_op (NOT? (LIKE | ILIKE | SIMILAR TO) a_expr_qual_op escape_?)?
    ;

/* 8*/

a_expr_qual_op
    : a_expr_unary_qualop (qual_op a_expr_unary_qualop)*
    ;

/* 9*/

a_expr_unary_qualop
    : qual_op? a_expr_add
    ;

/* 7*/

a_expr_add
    : a_expr_mul ((MINUS | PLUS) a_expr_mul)*
    ;

/* 6*/

a_expr_mul
    : a_expr_caret ((STAR | SLASH | PERCENT) a_expr_caret)*
    ;

/* 5*/

a_expr_caret
    : a_expr_unary_sign (CARET a_expr_unary_sign)?
    ;

/* 4*/

a_expr_unary_sign
    : (MINUS | PLUS)? a_expr_at_time_zone /* */
    ;

/* 3*/

a_expr_at_time_zone
    : a_expr_collate (AT TIME ZONE a_expr)?
    ;

/* 2*/

a_expr_collate
    : a_expr_typecast (COLLATE any_name)?
    ;

/* 1*/

a_expr_typecast
    : c_expr (TYPECAST typename)*
    ;

b_expr
    : c_expr
    | b_expr TYPECAST typename
    //right	unary plus, unary minus
    | (PLUS | MINUS) b_expr
    //^	left	exponentiation
    | b_expr CARET b_expr
    //* / %	left	multiplication, division, modulo
    | b_expr (STAR | SLASH | PERCENT) b_expr
    //+ -	left	addition, subtraction
    | b_expr (PLUS | MINUS) b_expr
    //(any other operator)	left	all other native and user-defined operators
    | b_expr qual_op b_expr
    //< > = <= >= <>	 	comparison operators
    | b_expr (LT | GT | EQUAL | LESS_EQUALS | GREATER_EQUALS | NOT_EQUALS) b_expr
    | qual_op b_expr
    | b_expr qual_op
    //S ISNULL NOTNULL	 	IS TRUE, IS FALSE, IS NULL, IS DISTINCT FROM, etc
    | b_expr IS NOT? (DISTINCT FROM b_expr | OF OPEN_PAREN type_list CLOSE_PAREN | DOCUMENT_P)
    ;

c_expr
    : EXISTS select_with_parens                                        # c_expr_exists
    | ARRAY (select_with_parens | array_expr)                          # c_expr_expr
    | PARAM opt_indirection                                            # c_expr_expr
    | GROUPING OPEN_PAREN expr_list CLOSE_PAREN                        # c_expr_expr
    | /*22*/ UNIQUE select_with_parens                                 # c_expr_expr
    | columnref                                                        # c_expr_expr
    | aexprconst                                                       # c_expr_expr
    | OPEN_PAREN a_expr_in_parens = a_expr CLOSE_PAREN opt_indirection # c_expr_expr
    | case_expr                                                        # c_expr_case
    | func_expr                                                        # c_expr_expr
    | select_with_parens indirection?                                  # c_expr_expr
    | explicit_row                                                     # c_expr_expr
    | implicit_row                                                     # c_expr_expr
    | row OVERLAPS row /* 14*/                                         # c_expr_expr
    | DEFAULT                                                          # c_expr_expr
    ;

plsqlvariablename
    : PLSQLVARIABLENAME
    ;

func_application
    : func_name OPEN_PAREN (
        func_arg_list (COMMA VARIADIC func_arg_expr)? sort_clause_?
        | VARIADIC func_arg_expr sort_clause_?
        | (ALL | DISTINCT) func_arg_list sort_clause_?
        | STAR
        |
    ) CLOSE_PAREN
    ;

func_expr
    : func_application within_group_clause? filter_clause? over_clause?
    | func_expr_common_subexpr
    ;

func_expr_windowless
    : func_application
    | func_expr_common_subexpr
    ;

func_expr_common_subexpr
    : COLLATION FOR OPEN_PAREN a_expr CLOSE_PAREN
    | CURRENT_DATE
    | CURRENT_TIME (OPEN_PAREN iconst CLOSE_PAREN)?
    | CURRENT_TIMESTAMP (OPEN_PAREN iconst CLOSE_PAREN)?
    | LOCALTIME (OPEN_PAREN iconst CLOSE_PAREN)?
    | LOCALTIMESTAMP (OPEN_PAREN iconst CLOSE_PAREN)?
    | CURRENT_ROLE
    | CURRENT_USER
    | SESSION_USER
    | SYSTEM_USER
    | USER
    | CURRENT_CATALOG
    | CURRENT_SCHEMA
    | CAST OPEN_PAREN a_expr AS typename CLOSE_PAREN
    | EXTRACT OPEN_PAREN extract_list? CLOSE_PAREN
    | NORMALIZE OPEN_PAREN a_expr (COMMA unicode_normal_form)? CLOSE_PAREN
    | OVERLAY OPEN_PAREN (overlay_list | func_arg_list? ) CLOSE_PAREN
    | POSITION OPEN_PAREN position_list? CLOSE_PAREN
    | SUBSTRING OPEN_PAREN (substr_list | func_arg_list?) CLOSE_PAREN
    | TREAT OPEN_PAREN a_expr AS typename CLOSE_PAREN
    | TRIM OPEN_PAREN (BOTH | LEADING | TRAILING)? trim_list CLOSE_PAREN
    | NULLIF OPEN_PAREN a_expr COMMA a_expr CLOSE_PAREN
    | COALESCE OPEN_PAREN expr_list CLOSE_PAREN
    | GREATEST OPEN_PAREN expr_list CLOSE_PAREN
    | LEAST OPEN_PAREN expr_list CLOSE_PAREN
    | XMLCONCAT OPEN_PAREN expr_list CLOSE_PAREN
    | XMLELEMENT OPEN_PAREN NAME_P colLabel (COMMA (xml_attributes | expr_list))? CLOSE_PAREN
    | XMLEXISTS OPEN_PAREN c_expr xmlexists_argument CLOSE_PAREN
    | XMLFOREST OPEN_PAREN xml_attribute_list CLOSE_PAREN
    | XMLPARSE OPEN_PAREN document_or_content a_expr xml_whitespace_option? CLOSE_PAREN
    | XMLPI OPEN_PAREN NAME_P colLabel (COMMA a_expr)? CLOSE_PAREN
    | XMLROOT OPEN_PAREN XML_P a_expr COMMA xml_root_version xml_root_standalone_? CLOSE_PAREN
    | XMLSERIALIZE OPEN_PAREN document_or_content a_expr AS simpletypename CLOSE_PAREN
    | JSON_OBJECT OPEN_PAREN (func_arg_list
		| json_name_and_value_list
		  json_object_constructor_null_clause?
		  json_key_uniqueness_constraint?
		  json_returning_clause?
		| json_returning_clause? )
		CLOSE_PAREN
    | JSON_ARRAY OPEN_PAREN (json_value_expr_list
		  json_array_constructor_null_clause?
		  json_returning_clause?
		| select_no_parens
		  json_format_clause?
		  json_returning_clause?
		| json_returning_clause?
		)
		CLOSE_PAREN
    | JSON '(' json_value_expr json_key_uniqueness_constraint? ')'
    | JSON_SCALAR '(' a_expr ')'
    | JSON_SERIALIZE '(' json_value_expr json_returning_clause? ')'
    | MERGE_ACTION '(' ')'
    | JSON_QUERY '('
		json_value_expr ',' a_expr json_passing_clause?
		json_returning_clause?
		json_wrapper_behavior
		json_quotes_clause?
		json_behavior_clause?
		')'
    | JSON_EXISTS '('
		json_value_expr ',' a_expr json_passing_clause?
		json_on_error_clause?
		')'
    | JSON_VALUE '('
		json_value_expr ',' a_expr json_passing_clause?
		json_returning_clause?
		json_behavior_clause?
		')'
    ;

/* SQL/XML support */

xml_root_version
    : VERSION_P a_expr
    | VERSION_P NO VALUE_P
    ;

xml_root_standalone_
    : COMMA STANDALONE_P YES_P
    | COMMA STANDALONE_P NO
    | COMMA STANDALONE_P NO VALUE_P
    ;

xml_attributes
    : XMLATTRIBUTES OPEN_PAREN xml_attribute_list CLOSE_PAREN
    ;

xml_attribute_list
    : xml_attribute_el (COMMA xml_attribute_el)*
    ;

xml_attribute_el
    : a_expr (AS colLabel)?
    ;

document_or_content
    : DOCUMENT_P
    | CONTENT_P
    ;

xml_whitespace_option
    : PRESERVE WHITESPACE_P
    | STRIP_P WHITESPACE_P
    
    ;

xmlexists_argument
    : PASSING c_expr
    | PASSING c_expr xml_passing_mech
    | PASSING xml_passing_mech c_expr
    | PASSING xml_passing_mech c_expr xml_passing_mech
    ;

xml_passing_mech
    : BY (REF | VALUE_P)
    ;

within_group_clause
    : WITHIN GROUP_P OPEN_PAREN sort_clause CLOSE_PAREN
    
    ;

filter_clause
    : FILTER OPEN_PAREN WHERE a_expr CLOSE_PAREN
    
    ;

window_clause
    : WINDOW window_definition_list
    
    ;

window_definition_list
    : window_definition (COMMA window_definition)*
    ;

window_definition
    : colid AS window_specification
    ;

over_clause
    : OVER (window_specification | colid)
    
    ;

window_specification
    : OPEN_PAREN existing_window_name_? partition_clause_? sort_clause_? frame_clause_? CLOSE_PAREN
    ;

existing_window_name_
    : colid
    
    ;

partition_clause_
    : PARTITION BY expr_list
    
    ;

frame_clause_
    : RANGE frame_extent window_exclusion_clause_?
    | ROWS frame_extent window_exclusion_clause_?
    | GROUPS frame_extent window_exclusion_clause_?
    
    ;

frame_extent
    : frame_bound
    | BETWEEN frame_bound AND frame_bound
    ;

frame_bound
    : UNBOUNDED (PRECEDING | FOLLOWING)
    | CURRENT_P ROW
    | a_expr (PRECEDING | FOLLOWING)
    ;

window_exclusion_clause_
    : EXCLUDE (CURRENT_P ROW | GROUP_P | TIES | NO OTHERS)
    
    ;

row
    : ROW OPEN_PAREN expr_list? CLOSE_PAREN
    | OPEN_PAREN expr_list COMMA a_expr CLOSE_PAREN
    ;

explicit_row
    : ROW OPEN_PAREN expr_list? CLOSE_PAREN
    ;

/*
TODO:
for some reason v1
implicit_row: OPEN_PAREN expr_list COMMA a_expr CLOSE_PAREN;
works better than v2
implicit_row: OPEN_PAREN expr_list  CLOSE_PAREN;
while looks like they are almost the same, except v2 requieres at least 2 items in list
while v1 allows single item in list
*/

implicit_row
    : OPEN_PAREN expr_list COMMA a_expr CLOSE_PAREN
    ;

sub_type
    : ANY
    | SOME
    | ALL
    ;

all_op
    : Operator
    | mathop
    ;

mathop
    : PLUS
    | MINUS
    | STAR
    | SLASH
    | PERCENT
    | CARET
    | LT
    | GT
    | EQUAL
    | LESS_EQUALS
    | GREATER_EQUALS
    | NOT_EQUALS
    ;

qual_op
    : Operator
    | OPERATOR OPEN_PAREN any_operator CLOSE_PAREN
    ;

qual_all_op
    : all_op
    | OPERATOR OPEN_PAREN any_operator CLOSE_PAREN
    ;

subquery_Op
    : all_op
    | OPERATOR OPEN_PAREN any_operator CLOSE_PAREN
    | LIKE
    | NOT LIKE
    | ILIKE
    | NOT ILIKE
    ;

expr_list
    : a_expr (COMMA a_expr)*
    ;

func_arg_list
    : func_arg_expr (COMMA func_arg_expr)*
    ;

func_arg_expr
    : a_expr
    | param_name (COLON_EQUALS | EQUALS_GREATER) a_expr
    ;

type_list
    : typename (COMMA typename)*
    ;

array_expr
    : OPEN_BRACKET (expr_list | array_expr_list)? CLOSE_BRACKET
    ;

array_expr_list
    : array_expr (COMMA array_expr)*
    ;

extract_list
    : extract_arg FROM a_expr
    
    ;

extract_arg
    : identifier
    | YEAR_P
    | MONTH_P
    | DAY_P
    | HOUR_P
    | MINUTE_P
    | SECOND_P
    | sconst
    ;

unicode_normal_form
    : NFC
    | NFD
    | NFKC
    | NFKD
    ;

overlay_list
    : a_expr PLACING a_expr FROM a_expr (FOR a_expr)?
    ;

position_list
    : b_expr IN_P b_expr
    
    ;

substr_list
    : a_expr FROM a_expr FOR a_expr
    | a_expr FOR a_expr FROM a_expr
    | a_expr FROM a_expr
    | a_expr FOR a_expr
    | a_expr SIMILAR a_expr ESCAPE a_expr
    ;

trim_list
    : a_expr FROM expr_list
    | FROM expr_list
    | expr_list
    ;

in_expr
    : select_with_parens               # in_expr_select
    | OPEN_PAREN expr_list CLOSE_PAREN # in_expr_list
    ;

case_expr
    : CASE case_arg? when_clause_list case_default? END_P
    ;

when_clause_list
    : when_clause+
    ;

when_clause
    : WHEN a_expr THEN a_expr
    ;

case_default
    : ELSE a_expr
    
    ;

case_arg
    : a_expr
    
    ;

columnref
    : colid indirection?
    ;

indirection_el
    : DOT (attr_name | STAR)
    | OPEN_BRACKET (a_expr | slice_bound_? COLON slice_bound_?) CLOSE_BRACKET
    ;

slice_bound_
    : a_expr
    
    ;

indirection
    : indirection_el+
    ;

opt_indirection
    : indirection_el*
    ;

/* SQL/JSON support */
json_passing_clause:
			PASSING json_arguments
		;

json_arguments:
			json_argument
			| json_arguments ',' json_argument
		;

json_argument:
			json_value_expr AS colLabel
		;

/* ARRAY is a noise word */
json_wrapper_behavior:
			  WITHOUT WRAPPER
			| WITHOUT ARRAY	WRAPPER
			| WITH WRAPPER
			| WITH ARRAY WRAPPER
			| WITH CONDITIONAL ARRAY WRAPPER
			| WITH UNCONDITIONAL ARRAY WRAPPER
			| WITH CONDITIONAL WRAPPER
			| WITH UNCONDITIONAL WRAPPER
			|
		;

json_behavior:
			DEFAULT a_expr
			| json_behavior_type
		;

json_behavior_type:
			ERROR
			| NULL_P
			| TRUE_P
			| FALSE_P
			| UNKNOWN
			| EMPTY_P ARRAY
			| EMPTY_P OBJECT_P
			/* non-standard, for Oracle compatibility only */
			| EMPTY_P
		;

json_behavior_clause:
			json_behavior ON EMPTY_P
			| json_behavior ON ERROR
			| json_behavior ON EMPTY_P json_behavior ON ERROR
		;

json_on_error_clause:
			json_behavior ON ERROR
		;

json_value_expr:
			a_expr json_format_clause?
		;

json_format_clause:
			FORMAT_LA JSON ENCODING name
			| FORMAT_LA JSON
		;


json_quotes_clause:
			KEEP QUOTES ON SCALAR STRING_P
			| KEEP QUOTES
			| OMIT QUOTES ON SCALAR STRING_P
			| OMIT QUOTES
		;

json_returning_clause:
			RETURNING typename json_format_clause?
		;

/*
 * We must assign the only-JSON production a precedence less than IDENT in
 * order to favor shifting over reduction when JSON is followed by VALUE_P,
 * OBJECT_P, or SCALAR.  (ARRAY doesn't need that treatment, because it's a
 * fully reserved word.)  Because json_predicate_type_constraint is always
 * followed by json_key_uniqueness_constraint_opt, we also need the only-JSON
 * production to have precedence less than WITH and WITHOUT.  UNBOUNDED isn't
 * really related to this syntax, but it's a convenient choice because it
 * already has a precedence less than IDENT for other reasons.
 */
json_predicate_type_constraint:
			JSON
			| JSON VALUE_P
			| JSON ARRAY
			| JSON OBJECT_P
			| JSON SCALAR
		;

/*
 * KEYS is a noise word here.  To avoid shift/reduce conflicts, assign the
 * KEYS-less productions a precedence less than IDENT (i.e., less than KEYS).
 * This prevents reducing them when the next token is KEYS.
 */
json_key_uniqueness_constraint:
			WITH UNIQUE KEYS
			| WITH UNIQUE
			| WITHOUT UNIQUE KEYS
			| WITHOUT UNIQUE
		;

json_name_and_value_list:
			json_name_and_value
			| json_name_and_value_list ',' json_name_and_value
		;

json_name_and_value:
			c_expr VALUE_P json_value_expr
			|
			a_expr ':' json_value_expr
		;

/* empty means false for objects, true for arrays */
json_object_constructor_null_clause:
			NULL_P ON NULL_P
			| ABSENT ON NULL_P
		;

json_array_constructor_null_clause:
			NULL_P ON NULL_P
			| ABSENT ON NULL_P
		;

json_value_expr_list:
			json_value_expr
			| json_value_expr_list ',' json_value_expr
		;

json_aggregate_func:
			JSON_OBJECTAGG '('
				json_name_and_value
				json_object_constructor_null_clause?
				json_key_uniqueness_constraint?
				json_returning_clause
			')'
			| JSON_ARRAYAGG '('
				json_value_expr
				json_array_aggregate_order_by_clause?
				json_array_constructor_null_clause?
				json_returning_clause
			')'
		;

json_array_aggregate_order_by_clause:
			ORDER BY sortby_list
		;

/*****************************************************************************
 *
 *	target list for SELECT
 *
 *****************************************************************************/

target_list_
    : target_list2
    ;

target_list1
    : target_el (COMMA target_el)*
    ;

target_list2
    : target_el2 (COMMA target_el2)*
    ;

target_el
    : a_expr (AS colLabel | bareColLabel |) # target_label
    | STAR                                # target_star
    ;

target_el2
    : aexprconst (AS colLabel | bareColLabel |)
    | qualified_name (AS colLabel | bareColLabel |)
    ;

qualified_name_list
    : qualified_name (COMMA qualified_name)*
    ;

qualified_name
    : colid indirection?
    ;

name_list
    : name (COMMA name)*
    ;

name
    : colid
    ;

attr_name
    : colLabel
    ;

file_name
    : sconst
    ;

func_name
    : type_function_name
    | colid indirection
    ;

aexprconst
    : iconst
    | fconst
    | sconst
    | bconst
    | xconst
    | func_name (sconst | OPEN_PAREN func_arg_list sort_clause_? CLOSE_PAREN sconst)
    | consttypename sconst
    | constinterval (sconst interval_? | OPEN_PAREN iconst CLOSE_PAREN sconst)
    | TRUE_P
    | FALSE_P
    | NULL_P
    ;

xconst
    : HexadecimalStringConstant
    ;

bconst
    : BinaryStringConstant
    ;

fconst
    : Numeric
    ;

iconst
    : Integral
    | BinaryIntegral
    | OctalIntegral
    | HexadecimalIntegral
    ;

sconst
    : anysconst uescape_?
    ;

anysconst
    : StringConstant
    | UnicodeEscapeStringConstant
    | BeginDollarStringConstant DollarText* EndDollarStringConstant
    | EscapeStringConstant
    ;

uescape_
    : UESCAPE anysconst
    
    ;

signediconst
    : iconst
    | PLUS iconst
    | MINUS iconst
    ;

rolespec
    : nonreservedword
    | CURRENT_USER
    | SESSION_USER
    ;

role_list
    : rolespec (COMMA rolespec)*
    ;

/*
 * Name classification hierarchy.
 *
 * IDENT is the lexeme returned by the lexer for identifiers that match
 * no known keyword.  In most cases, we can accept certain keywords as
 * names, not only IDENTs.	We prefer to accept as many such keywords
 * as possible to minimize the impact of "reserved words" on programmers.
 * So, we divide names into several possible classes.  The classification
 * is chosen in part to make keywords acceptable as names wherever possible.
 */

/* Column identifier --- names that can be column, table, etc names.
 */
colid
    : identifier
    | unreserved_keyword
    | col_name_keyword
    ;

/* Type/function identifier --- names that can be type or function names.
 */
type_function_name
    : identifier
    | unreserved_keyword
    | type_func_name_keyword
    ;

/* Any not-fully-reserved word --- these names can be, eg, role names.
 */
nonreservedword
    : identifier
    | unreserved_keyword
    | col_name_keyword
    | type_func_name_keyword
    ;

/* Column label --- allowed labels in "AS" clauses.
 * This presently includes *all* Postgres keywords.
 */
colLabel
    : identifier
    | unreserved_keyword
    | col_name_keyword
    | type_func_name_keyword
    | reserved_keyword
    | EXIT //NB: not in gram.y official source.
    ;

/* Bare column label --- names that can be column labels without writing "AS".
 * This classification is orthogonal to the other keyword categories.
 */
bareColLabel
    : identifier
    | bare_label_keyword
    ;

/*
 * Keyword category lists.  Generally, every keyword present in
 * the Postgres grammar should appear in exactly one of these lists.
 *
 * Put a new keyword into the first list that it can go into without causing
 * shift or reduce conflicts.  The earlier lists define "less reserved"
 * categories of keywords.
 *
 * Make sure that each keyword's category in kwlist.h matches where
 * it is listed here.  (Someday we may be able to generate these lists and
 * kwlist.h's table from one source of truth.)
 */

/* "Unreserved" keywords --- available for use as any kind of name.
 */
unreserved_keyword
    : ABORT_P
    | ABSENT
    | ABSOLUTE_P
    | ACCESS
    | ACTION
    | ADD_P
    | ADMIN
    | AFTER
    | AGGREGATE
    | ALSO
    | ALTER
    | ALWAYS
    | ASENSITIVE
    | ASSERTION
    | ASSIGNMENT
    | AT
    | ATOMIC
    | ATTACH
    | ATTRIBUTE
    | BACKWARD
    | BEFORE
    | BEGIN_P
    | BREADTH
    | BY
    | CACHE
    | CALL
    | CALLED
    | CASCADE
    | CASCADED
    | CATALOG
    | CHAIN
    | CHARACTERISTICS
    | CHECKPOINT
    | CLASS
    | CLOSE
    | CLUSTER
    | COLUMNS
    | COMMENT
    | COMMENTS
    | COMMIT
    | COMMITTED
    | COMPRESSION
    | CONDITIONAL
    | CONFIGURATION
    | CONFLICT
    | CONNECTION
    | CONSTRAINTS
    | CONTENT_P
    | CONTINUE_P
    | CONVERSION_P
    | COPY
    | COST
    | CSV
    | CUBE
    | CURRENT_P
    | CURSOR
    | CYCLE
    | DATA_P
    | DAY_P
    | DEALLOCATE
    | DECLARE
    | DEFAULTS
    | DEFERRED
    | DEFINER
    | DELETE_P
    | DELIMITER
    | DELIMITERS
    | DEPENDS
    | DEPTH
    | DETACH
    | DICTIONARY
    | DISABLE_P
    | DISCARD
    | DOCUMENT_P
    | DOMAIN_P
    | DOUBLE_P
    | DROP
    | EACH
    | EMPTY_P
    | ENABLE_P
    | ENCODING
    | ENCRYPTED
    | ENUM_P
    | ERROR
    | ESCAPE
    | EVENT
    | EXCLUDE
    | EXCLUDING
    | EXCLUSIVE
    | EXECUTE
    | EXPLAIN
    | EXPRESSION
    | EXTENSION
    | EXTERNAL
    | FAMILY
    | FILTER
    | FINALIZE
    | FIRST_P
    | FOLLOWING
    | FORCE
    | FORMAT
    | FORWARD
    | FUNCTION
    | FUNCTIONS
    | GENERATED
    | GLOBAL
    | GRANTED
    | GROUPS
    | HANDLER
    | HEADER_P
    | HOLD
    | HOUR_P
    | IDENTITY_P
    | IF_P
    | IMMEDIATE
    | IMMUTABLE
    | IMPLICIT_P
    | IMPORT_P
    | INCLUDE
    | INCLUDING
    | INCREMENT
    | INDENT
    | INDEX
    | INDEXES
    | INHERIT
    | INHERITS
    | INLINE_P
    | INPUT_P
    | INSENSITIVE
    | INSERT
    | INSTEAD
    | INVOKER
    | ISOLATION
    | KEEP
    | KEY
    | KEYS
    | LABEL
    | LANGUAGE
    | LARGE_P
    | LAST_P
    | LEAKPROOF
    | LEVEL
    | LISTEN
    | LOAD
    | LOCAL
    | LOCATION
    | LOCK_P
    | LOCKED
    | LOGGED
    | MAPPING
    | MATCH
    | MATCHED
    | MATERIALIZED
    | MAXVALUE
    | MERGE
    | METHOD
    | MINUTE_P
    | MINVALUE
    | MODE
    | MONTH_P
    | MOVE
    | NAME_P
    | NAMES
    | NESTED
    | NEW
    | NEXT
    | NFC
    | NFD
    | NFKC
    | NFKD
    | NO
    | NORMALIZED
    | NOTHING
    | NOTIFY
    | NOWAIT
    | NULLS_P
    | OBJECT_P
    | OF
    | OFF
    | OIDS
    | OLD
    | OMIT
    | OPERATOR
    | OPTION
    | OPTIONS
    | ORDINALITY
    | OTHERS
    | OVER
    | OVERRIDING
    | OWNED
    | OWNER
    | PARALLEL
    | PARAMETER
    | PARSER
    | PARTIAL
    | PARTITION
    | PASSING
    | PASSWORD
    | PATH
    | PERIOD
    | PLAN
    | PLANS
    | POLICY
    | PRECEDING
    | PREPARE
    | PREPARED
    | PRESERVE
    | PRIOR
    | PRIVILEGES
    | PROCEDURAL
    | PROCEDURE
    | PROCEDURES
    | PROGRAM
    | PUBLICATION
    | QUOTE
    | QUOTES
    | RANGE
    | READ
    | REASSIGN
//    | RECHECK
    | RECURSIVE
    | REF
    | REFERENCING
    | REFRESH
    | REINDEX
    | RELATIVE_P
    | RELEASE
    | RENAME
    | REPEATABLE
    | REPLACE
    | REPLICA
    | RESET
    | RESTART
    | RESTRICT
    | RETURN
    | RETURNS
    | REVOKE
    | ROLE
    | ROLLBACK
    | ROLLUP
    | ROUTINE
    | ROUTINES
    | ROWS
    | RULE
    | SAVEPOINT
    | SCALAR
    | SCHEMA
    | SCHEMAS
    | SCROLL
    | SEARCH
    | SECOND_P
    | SECURITY
    | SEQUENCE
    | SEQUENCES
    | SERIALIZABLE
    | SERVER
    | SESSION
    | SET
    | SETS
    | SHARE
    | SHOW
    | SIMPLE
    | SKIP_P
    | SNAPSHOT
    | SOURCE
    | SQL_P
    | STABLE
    | STANDALONE_P
    | START
    | STATEMENT
    | STATISTICS
    | STDIN
    | STDOUT
    | STORAGE
    | STORED
    | STRICT_P
    | STRING_P
    | STRIP_P
    | SUBSCRIPTION
    | SUPPORT
    | SYSID
    | SYSTEM_P
    | TABLES
    | TABLESPACE
    | TARGET
    | TEMP
    | TEMPLATE
    | TEMPORARY
    | TEXT_P
    | TIES
    | TRANSACTION
    | TRANSFORM
    | TRIGGER
    | TRUNCATE
    | TRUSTED
    | TYPE_P
    | TYPES_P
    | UESCAPE
    | UNBOUNDED
    | UNCOMMITTED
    | UNCONDITIONAL
    | UNENCRYPTED
    | UNKNOWN
    | UNLISTEN
    | UNLOGGED
    | UNTIL
    | UPDATE
    | VACUUM
    | VALID
    | VALIDATE
    | VALIDATOR
    | VALUE_P
    | VARYING
    | VERSION_P
    | VIEW
    | VIEWS
    | VOLATILE
    | WHITESPACE_P
    | WITHIN
    | WITHOUT
    | WORK
    | WRAPPER
    | WRITE
    | XML_P
    | YEAR_P
    | YES_P
    | ZONE
    ;

/* Column identifier --- keywords that can be column, table, etc names.
 *
 * Many of these keywords will in fact be recognized as type or function
 * names too; but they have special productions for the purpose, and so
 * can't be treated as "generic" type or function names.
 *
 * The type names appearing here are not usable as function names
 * because they can be followed by '(' in typename productions, which
 * looks too much like a function call for an LR(1) parser.
 */
col_name_keyword
    : BETWEEN
    | BIGINT
    | BIT
    | BOOLEAN_P
    | CHAR_P
    | character
    | COALESCE
    | DEC
    | DECIMAL_P
    | EXISTS
    | EXTRACT
    | FLOAT_P
    | GREATEST
    | GROUPING
    | INOUT
    | INT_P
    | INTEGER
    | INTERVAL
    | JSON
    | JSON_ARRAY
    | JSON_ARRAYAGG
    | JSON_EXISTS
    | JSON_OBJECT
    | JSON_OBJECTAGG
    | JSON_QUERY
    | JSON_SCALAR
    | JSON_SERIALIZE
    | JSON_TABLE
    | JSON_VALUE
    | LEAST
    | MERGE_ACTION
    | NATIONAL
    | NCHAR
    | NONE
    | NORMALIZE
    | NULLIF
    | NUMERIC
    | OUT_P
    | OVERLAY
    | POSITION
    | PRECISION
    | REAL
    | ROW
    | SETOF
    | SMALLINT
    | SUBSTRING
    | TIME
    | TIMESTAMP
    | TREAT
    | TRIM
    | VALUES
    | VARCHAR
    | XMLATTRIBUTES
    | XMLCONCAT
    | XMLELEMENT
    | XMLEXISTS
    | XMLFOREST
    | XMLNAMESPACES
    | XMLPARSE
    | XMLPI
    | XMLROOT
    | XMLSERIALIZE
    | XMLTABLE
    ;

/* Type/function identifier --- keywords that can be type or function names.
 *
 * Most of these are keywords that are used as operators in expressions;
 * in general such keywords can't be column names because they would be
 * ambiguous with variables, but they are unambiguous as function identifiers.
 *
 * Do not include POSITION, SUBSTRING, etc here since they have explicit
 * productions in a_expr to support the goofy SQL9x argument syntax.
 * - thomas 2000-11-28
 */
type_func_name_keyword
    : AUTHORIZATION
    | BINARY
    | COLLATION
    | CONCURRENTLY
    | CROSS
    | CURRENT_SCHEMA
    | FREEZE
    | FULL
    | ILIKE
    | INNER_P
    | IS
    | ISNULL
    | JOIN
    | LEFT
    | LIKE
    | NATURAL
    | NOTNULL
    | OUTER_P
    | OVERLAPS
    | RIGHT
    | SIMILAR
    | TABLESAMPLE
    | VERBOSE
    ;

/* Reserved keyword --- these keywords are usable only as a ColLabel.
 *
 * Keywords appear here if they could not be distinguished from variable,
 * type, or function names in some contexts.  Don't put things here unless
 * forced to.
 */
reserved_keyword
    : ALL
    | ANALYSE
    | ANALYZE
    | AND
    | ANY
    | ARRAY
    | AS
    | ASC
    | ASYMMETRIC
    | BOTH
    | CASE
    | CAST
    | COLLATE
    | COLUMN
    | CONSTRAINT
    | CREATE
    | CURRENT_CATALOG
    | CURRENT_DATE
    | CURRENT_ROLE
    | CURRENT_TIME
    | CURRENT_TIMESTAMP
    | CURRENT_USER
    | DEFAULT
    | DEFERRABLE
    | DESC
    | DISTINCT
    | DO
    | ELSE
    | END_P
    | ENTITY
    | EXCEPT
    | FALSE_P
    | FETCH
    | FOR
    | FOREIGN
    | FROM
    | GRANT
    | GROUP_P
    | HAVING
    | IN_P
    | INITIALLY
    | INTERSECT
    | INTO
    | LATERAL_P
    | LEADING
    | LIMIT
    | LOCALTIME
    | LOCALTIMESTAMP
    | NOT
    | NULL_P
    | OFFSET
    | ON
    | ONLY
    | OR
    | ORDER
    | PLACING
    | PRIMARY
    | REFERENCES
    | RELATIONSHIP
    | RETURNING
    | SELECT
    | SESSION_USER
    | SOME
    | SYMMETRIC
    | SYSTEM_USER
    | THEN
    | TO
    | TRAILING
    | TRUE_P
    | UNION
    | UNIQUE
    | USER
    | USING
    | VARIADIC
    | WHEN
    | WHERE
    | WINDOW
    | WITH
    ;

/*
 * While all keywords can be used as column labels when preceded by AS,
 * not all of them can be used as a "bare" column label without AS.
 * Those that can be used as a bare label must be listed here,
 * in addition to appearing in one of the category lists above.
 *
 * Always add a new keyword to this list if possible.  Mark it BARE_LABEL
 * in kwlist.h if it is included here, or AS_LABEL if it is not.
 */
bare_label_keyword
    : ABORT_P
    | ABSENT
    | ABSOLUTE_P
    | ACCESS
    | ACTION
    | ADD_P
    | ADMIN
    | AFTER
    | AGGREGATE
    | ALL
    | ALSO
    | ALTER
    | ALWAYS
    | ANALYSE
    | ANALYZE
    | AND
    | ANY
    | ASC
    | ASENSITIVE
    | ASSERTION
    | ASSIGNMENT
    | ASYMMETRIC
    | AT
    | ATOMIC
    | ATTACH
    | ATTRIBUTE
    | AUTHORIZATION
    | BACKWARD
    | BEFORE
    | BEGIN_P
    | BETWEEN
    | BIGINT
    | BINARY
    | BIT
    | BOOLEAN_P
    | BOTH
    | BREADTH
    | BY
    | CACHE
    | CALL
    | CALLED
    | CASCADE
    | CASCADED
    | CASE
    | CAST
    | CATALOG
    | CHAIN
    | CHARACTERISTICS
    | CHECKPOINT
    | CLASS
    | CLOSE
    | CLUSTER
    | COALESCE
    | COLLATE
    | COLLATION
    | COLUMN
    | COLUMNS
    | COMMENT
    | COMMENTS
    | COMMIT
    | COMMITTED
    | COMPRESSION
    | CONCURRENTLY
    | CONDITIONAL
    | CONFIGURATION
    | CONFLICT
    | CONNECTION
    | CONSTRAINT
    | CONSTRAINTS
    | CONTENT_P
    | CONTINUE_P
    | CONVERSION_P
    | COPY
    | COST
    | CROSS
    | CSV
    | CUBE
    | CURRENT_CATALOG
    | CURRENT_DATE
    | CURRENT_P
    | CURRENT_ROLE
    | CURRENT_SCHEMA
    | CURRENT_TIME
    | CURRENT_TIMESTAMP
    | CURRENT_USER
    | CURSOR
    | CYCLE
    | DATA_P
    | DEALLOCATE
    | DEC
    | DECIMAL_P
    | DECLARE
    | DEFAULT
    | DEFAULTS
    | DEFERRABLE
    | DEFERRED
    | DEFINER
    | DELETE_P
    | DELIMITER
    | DELIMITERS
    | DEPENDS
    | DEPTH
    | DESC
    | DETACH
    | DICTIONARY
    | DISABLE_P
    | DISCARD
    | DISTINCT
    | DO
    | DOCUMENT_P
    | DOMAIN_P
    | DOUBLE_P
    | DROP
    | EACH
    | ELSE
    | EMPTY_P
    | ENABLE_P
    | ENCODING
    | ENCRYPTED
    | END_P
    | ENUM_P
    | ERROR
    | ESCAPE
    | EVENT
    | EXCLUDE
    | EXCLUDING
    | EXCLUSIVE
    | EXECUTE
    | EXISTS
    | EXPLAIN
    | EXPRESSION
    | EXTENSION
    | EXTERNAL
    | EXTRACT
    | FALSE_P
    | FAMILY
    | FINALIZE
    | FIRST_P
    | FLOAT_P
    | FOLLOWING
    | FORCE
    | FOREIGN
    | FORMAT
    | FORWARD
    | FREEZE
    | FULL
    | FUNCTION
    | FUNCTIONS
    | GENERATED
    | GLOBAL
    | GRANTED
    | GREATEST
    | GROUPING
    | GROUPS
    | HANDLER
    | HEADER_P
    | HOLD
    | IDENTITY_P
    | IF_P
    | ILIKE
    | IMMEDIATE
    | IMMUTABLE
    | IMPLICIT_P
    | IMPORT_P
    | IN_P
    | INCLUDE
    | INCLUDING
    | INCREMENT
    | INDENT
    | INDEX
    | INDEXES
    | INHERIT
    | INHERITS
    | INITIALLY
    | INLINE_P
    | INNER_P
    | INOUT
    | INPUT_P
    | INSENSITIVE
    | INSERT
    | INSTEAD
    | INT_P
    | INTEGER
    | INTERVAL
    | INVOKER
    | IS
    | ISOLATION
    | JOIN
    | JSON
    | JSON_ARRAY
    | JSON_ARRAYAGG
    | JSON_EXISTS
    | JSON_OBJECT
    | JSON_OBJECTAGG
    | JSON_QUERY
    | JSON_SCALAR
    | JSON_SERIALIZE
    | JSON_TABLE
    | JSON_VALUE
    | KEEP
    | KEY
    | KEYS
    | LABEL
    | LANGUAGE
    | LARGE_P
    | LAST_P
    | LATERAL_P
    | LEADING
    | LEAKPROOF
    | LEAST
    | LEFT
    | LEVEL
    | LIKE
    | LISTEN
    | LOAD
    | LOCAL
    | LOCALTIME
    | LOCALTIMESTAMP
    | LOCATION
    | LOCK_P
    | LOCKED
    | LOGGED
    | MAPPING
    | MATCH
    | MATCHED
    | MATERIALIZED
    | MAXVALUE
    | MERGE
    | MERGE_ACTION
    | METHOD
    | MINVALUE
    | MODE
    | MOVE
    | NAME_P
    | NAMES
    | NATIONAL
    | NATURAL
    | NCHAR
    | NESTED
    | NEW
    | NEXT
    | NFC
    | NFD
    | NFKC
    | NFKD
    | NO
    | NONE
    | NORMALIZE
    | NORMALIZED
    | NOT
    | NOTHING
    | NOTIFY
    | NOWAIT
    | NULL_P
    | NULLIF
    | NULLS_P
    | NUMERIC
    | OBJECT_P
    | OF
    | OFF
    | OIDS
    | OLD
    | OMIT
    | ONLY
    | OPERATOR
    | OPTION
    | OPTIONS
    | OR
    | ORDINALITY
    | OTHERS
    | OUT_P
    | OUTER_P
    | OVERLAY
    | OVERRIDING
    | OWNED
    | OWNER
    | PARALLEL
    | PARAMETER
    | PARSER
    | PARTIAL
    | PARTITION
    | PASSING
    | PASSWORD
    | PATH
    | PERIOD
    | PLACING
    | PLAN
    | PLANS
    | POLICY
    | POSITION
    | PRECEDING
    | PREPARE
    | PREPARED
    | PRESERVE
    | PRIMARY
    | PRIOR
    | PRIVILEGES
    | PROCEDURAL
    | PROCEDURE
    | PROCEDURES
    | PROGRAM
    | PUBLICATION
    | QUOTE
    | QUOTES
    | RANGE
    | READ
    | REAL
    | REASSIGN
    | RECURSIVE
    | REF
    | REFERENCES
    | REFERENCING
    | REFRESH
    | REINDEX
    | RELATIVE_P
    | RELEASE
    | RENAME
    | REPEATABLE
    | REPLACE
    | REPLICA
    | RESET
    | RESTART
    | RESTRICT
    | RETURN
    | RETURNS
    | REVOKE
    | RIGHT
    | ROLE
    | ROLLBACK
    | ROLLUP
    | ROUTINE
    | ROUTINES
    | ROW
    | ROWS
    | RULE
    | SAVEPOINT
    | SCALAR
    | SCHEMA
    | SCHEMAS
    | SCROLL
    | SEARCH
    | SECURITY
    | SELECT
    | SEQUENCE
    | SEQUENCES
    | SERIALIZABLE
    | SERVER
    | SESSION
    | SESSION_USER
    | SET
    | SETOF
    | SETS
    | SHARE
    | SHOW
    | SIMILAR
    | SIMPLE
    | SKIP_P
    | SMALLINT
    | SNAPSHOT
    | SOME
    | SOURCE
    | SQL_P
    | STABLE
    | STANDALONE_P
    | START
    | STATEMENT
    | STATISTICS
    | STDIN
    | STDOUT
    | STORAGE
    | STORED
    | STRICT_P
    | STRING_P
    | STRIP_P
    | SUBSCRIPTION
    | SUBSTRING
    | SUPPORT
    | SYMMETRIC
    | SYSID
    | SYSTEM_P
    | SYSTEM_USER
    | TABLES
    | TABLESAMPLE
    | TABLESPACE
    | TARGET
    | TEMP
    | TEMPLATE
    | TEMPORARY
    | TEXT_P
    | THEN
    | TIES
    | TIME
    | TIMESTAMP
    | TRAILING
    | TRANSACTION
    | TRANSFORM
    | TREAT
    | TRIGGER
    | TRIM
    | TRUE_P
    | TRUNCATE
    | TRUSTED
    | TYPE_P
    | TYPES_P
    | UESCAPE
    | UNBOUNDED
    | UNCOMMITTED
    | UNCONDITIONAL
    | UNENCRYPTED
    | UNIQUE
    | UNKNOWN
    | UNLISTEN
    | UNLOGGED
    | UNTIL
    | UPDATE
    | USER
    | USING
    | VACUUM
    | VALID
    | VALIDATE
    | VALIDATOR
    | VALUE_P
    | VALUES
    | VARCHAR
    | VARIADIC
    | VERBOSE
    | VERSION_P
    | VIEW
    | VIEWS
    | VOLATILE
    | WHEN
    | WHITESPACE_P
    | WORK
    | WRAPPER
    | WRITE
    | XML_P
    | XMLATTRIBUTES
    | XMLCONCAT
    | XMLELEMENT
    | XMLEXISTS
    | XMLFOREST
    | XMLNAMESPACES
    | XMLPARSE
    | XMLPI
    | XMLROOT
    | XMLSERIALIZE
    | XMLTABLE
    | YES_P
    | ZONE
    ;


any_identifier
    : colid
    ;

identifier
    : Identifier uescape_?
    | QuotedIdentifier
    | UnicodeQuotedIdentifier
    | PLSQLVARIABLENAME
    ;

