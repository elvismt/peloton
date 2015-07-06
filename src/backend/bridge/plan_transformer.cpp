/*-------------------------------------------------------------------------
 *
 * plan_transformer.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/bridge/plan_transformer.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "nodes/pprint.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "bridge/bridge.h"
#include "executor/executor.h"
#include "parser/parsetree.h"

#include "backend/common/logger.h"
#include "backend/bridge/plan_transformer.h"
#include "backend/bridge/tuple_transformer.h"
#include "backend/common/logger.h"
#include "backend/expression/abstract_expression.h"
#include "backend/storage/data_table.h"
#include "backend/planner/delete_node.h"
#include "backend/planner/insert_node.h"
#include "backend/planner/seq_scan_node.h"

#include <cstring>

void printPlanStateTree(const PlanState * planstate);

namespace peloton {
namespace bridge {

/**
 * @brief Pretty print the plan state tree.
 * @return none.
 */
void PlanTransformer::PrintPlanState(const PlanState *plan_state) {
  printPlanStateTree(plan_state);
}

/**
 * @brief Convert Postgres PlanState into AbstractPlanNode.
 * @return Pointer to the constructed AbstractPlanNode.
 */
planner::AbstractPlanNode *PlanTransformer::TransformPlan(
    const PlanState *plan_state) {

  Plan *plan = plan_state->plan;
  planner::AbstractPlanNode *plan_node;

  switch (nodeTag(plan)) {
    case T_ModifyTable:
      plan_node = PlanTransformer::TransformModifyTable(
          reinterpret_cast<const ModifyTableState *>(plan_state));
      break;
    case T_SeqScan:
      plan_node = PlanTransformer::TransformSeqScan(
          reinterpret_cast<const SeqScanState*>(plan_state));
      break;
    case T_Result:
      plan_node = PlanTransformer::TransformResult(
          reinterpret_cast<const ResultState*>(plan_state));
    default:
      plan_node = nullptr;
      break;
  }

  return plan_node;
}

/**
 * @brief Convert ModifyTableState into AbstractPlanNode.
 * @return Pointer to the constructed AbstractPlanNode.
 *
 * Basically, it multiplexes into helper methods based on operation type.
 */
planner::AbstractPlanNode *PlanTransformer::TransformModifyTable(
    const ModifyTableState *mt_plan_state) {

  /* TODO: Add logging */
  ModifyTable *plan = (ModifyTable *) mt_plan_state->ps.plan;

  switch (plan->operation) {
    case CMD_INSERT:
      return PlanTransformer::TransformInsert(mt_plan_state);
      break;
    case CMD_UPDATE:
      return PlanTransformer::TransformUpdate(mt_plan_state);
      break;
    case CMD_DELETE:
      return PlanTransformer::TransformDelete(mt_plan_state);
      break;
    default:
      break;
  }

  return nullptr;
}

/**
 * @brief Convert ModifyTableState Insert case into AbstractPlanNode.
 * @return Pointer to the constructed AbstractPlanNode.
 */
planner::AbstractPlanNode *PlanTransformer::TransformInsert(
    const ModifyTableState *mt_plan_state) {

  /* Resolve result table */
  ResultRelInfo *result_rel_info = mt_plan_state->resultRelInfo;
  Relation result_relation_desc = result_rel_info->ri_RelationDesc;

  /* Currently, we only support plain insert statement.
   * So, the number of subplan must be exactly 1.
   * TODO: can it be 0? */

  Oid database_oid = GetCurrentDatabaseOid();
  Oid table_oid = result_relation_desc->rd_id;

  LOG_INFO("Insert into table with Oid %u", table_oid);

  /* Get the target table */
  storage::DataTable *target_table =
      static_cast<storage::DataTable*>(catalog::Manager::GetInstance()
          .GetLocation(database_oid, table_oid));

  if(target_table == nullptr) {
    LOG_ERROR("Target table is not found : database oid %u table oid %u", database_oid, table_oid);
    return nullptr;
  }

  LOG_INFO("Target table found : database oid %u table oid %u", database_oid, table_oid);

  /* Get the tuple schema */
  auto schema = target_table->GetSchema();

  /* Should be only one which is a Result Plan */
  assert(mt_plan_state->mt_nplans == 1);
  assert(mt_plan_state->mt_plans != nullptr);
  PlanState *subplan_state = mt_plan_state->mt_plans[0];
  std::vector<storage::Tuple *> tuples;


  auto result_plan_node = PlanTransformer::TransformResult(reinterpret_cast<ResultState*>(subplan_state));
  /*
   * We are only making the plan,
   * so we should definitely not call ExecProcNode() here.
   * In Postgres, tuple-to-insert is retrieved from a child plan
   * called "Result".
   * Shall we make something similar in Peloton?

  plan_slot = ExecProcNode(subplan_state);
  assert(!TupIsNull(plan_slot));  // The tuple should not be null

  auto tuple = TupleTransformer(plan_slot, schema);

  std::cout << (*tuple);

  tuples.push_back(tuple);
  */
  auto plan_node = new planner::InsertNode(target_table, tuples);

  return plan_node;
}

planner::AbstractPlanNode* PlanTransformer::TransformUpdate(
    const ModifyTableState* mt_plan_state) {

  return nullptr;
}

/**
 * @brief Convert a Postgres ModifyTableState with DELETE operation
 * into a Peloton DeleteNode.
 * @return Pointer to the constructed AbstractPlanNode.
 *
 * Just like Peloton,
 * the delete plan state in Postgres simply deletes tuples
 *  returned by a subplan.
 * So we don't need to handle predicates locally .
 */
planner::AbstractPlanNode* PlanTransformer::TransformDelete(
    const ModifyTableState* mt_plan_state) {

  // Grab Database ID and Table ID
  assert(mt_plan_state->resultRelInfo);  // Input must come from a subplan
  assert(mt_plan_state->mt_nplans == 1);  // Maybe relax later. I don't know when they can have >1 subplans.
  Oid database_oid = GetCurrentDatabaseOid();
  Oid table_oid = mt_plan_state->resultRelInfo[0].ri_RelationDesc->rd_id;

  /* Grab the target table */
  storage::DataTable *target_table =
      static_cast<storage::DataTable*>(catalog::Manager::GetInstance()
          .GetLocation(database_oid, table_oid));

  /* Grab the subplan -> child plan node */
  assert(mt_plan_state->mt_nplans == 1);
  PlanState *sub_planstate = mt_plan_state->mt_plans[0];

  bool truncate = false;

  // Create the peloton plan node
  auto plan_node = new planner::DeleteNode(target_table, truncate);

  // Add child plan node(s)
  plan_node->AddChild(TransformPlan(sub_planstate));

  return plan_node;
}

/**
 * @brief Convert a Postgres SeqScanState into a Peloton SeqScanNode.
 * @return Pointer to the constructed AbstractPlanNode.
 *
 * TODO: Can we also scan result from a child operator? (Non-base-table scan?)
 * We can't for now, but Postgres can.
 */
planner::AbstractPlanNode* PlanTransformer::TransformSeqScan(
    const SeqScanState* ss_plan_state) {

  assert(nodeTag(ss_plan_state) == T_SeqScanState);

  // Grab Database ID and Table ID
  assert(ss_plan_state->ss_currentRelation);  // Null if not a base table scan
  Oid database_oid = GetCurrentDatabaseOid();
  Oid table_oid = ss_plan_state->ss_currentRelation->rd_id;

  /* Grab the target table */
  storage::DataTable *target_table =
      static_cast<storage::DataTable*>(catalog::Manager::GetInstance()
          .GetLocation(database_oid, table_oid));

  /*
   * Grab and transform the predicate.
   *
   * TODO:
   * The qualifying predicate should be extracted from:
   * ss_plan_state->ps.qual (null if no predicate)
   *
   * Let's just use a null predicate for now.
   */
  expression::AbstractExpression* predicate = nullptr;

  /*
   * Grab and transform the output column Id's.
   *
   * TODO:
   * The output columns should be extracted from:
   * ss_plan_state->ps.ps_ProjInfo  (null if no projection)
   *
   * Let's just select all columns for now
   */
  auto schema = target_table->GetSchema();
  std::vector<oid_t> column_ids(schema->GetColumnCount());
  std::iota(column_ids.begin(), column_ids.end(), 0);
  assert(column_ids.size() > 0);

  /* Construct and return the Peloton plan node */
  auto plan_node = new planner::SeqScanNode(target_table, predicate,
                                            column_ids);
  return plan_node;
}

/**
 * @brief Convert a Postgres ResultState into a Peloton ResultPlanNode
 * @return Pointer to the constructed AbstractPlanNode
 */
planner::AbstractPlanNode *PlanTransformer::TransformResult(
    const ResultState *node) {
  ProjectionInfo *projInfo = node->ps.ps_ProjInfo;
  int numSimpleVars = projInfo->pi_numSimpleVars;
  ExprDoneCond *itemIsDone = projInfo->pi_itemIsDone;
	ExprContext *econtext = projInfo->pi_exprContext;

  if (node->rs_checkqual) {
    LOG_INFO("We can not handle constant qualifications now");
  }

  if (numSimpleVars > 0) {
    LOG_INFO("We can not handle simple vars now");
  }

  if (projInfo->pi_targetlist) {
    ListCell *tl;
    List *targetList = projInfo->pi_targetlist;

    LOG_INFO("The number of target in list is %d", list_length(targetList));


    foreach(tl, targetList)
    {
      GenericExprState *gstate = (GenericExprState *) lfirst(tl);
      TargetEntry *tle = (TargetEntry *) gstate->xprstate.expr;
      AttrNumber resind = tle->resno - 1;
      bool isnull;
      Datum value = ExecEvalExpr(gstate->arg,
									  econtext,
									  &isnull,
									  &itemIsDone[resind]);
      int integer = DatumGetInt32(value);
      LOG_INFO("The datum is %d", integer);
    }

  } else {
    LOG_INFO("We can not handle case where targelist is null");
  }
  return nullptr;
}

}  // namespace bridge
}  // namespace peloton