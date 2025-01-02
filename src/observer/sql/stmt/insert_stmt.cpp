/* Copyright (c) 2021OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/insert_stmt.h"
#include "common/log/log.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

InsertStmt::InsertStmt(Table *table, std::vector<const Value *> values, std::vector<int> value_amount)
    : table_(table), values_(values), value_amount_(value_amount)
{}

RC InsertStmt::create(Db *db, const InsertSqlNode &inserts, Stmt *&stmt)
{
  const char *table_name = inserts.relation_name.c_str();
  if (nullptr == db || nullptr == table_name || inserts.values.empty()) {
    LOG_WARN("invalid argument. db=%p, table_name=%p, value_num=%d",
        db, table_name, static_cast<int>(inserts.values.size()));
    return RC::INVALID_ARGUMENT;
  }

  // check whether the table exists
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // check the fields number
  std::vector<const Value*> valuess;
  std::vector<int> value_nums;
  const TableMeta &table_meta = table->table_meta();
  const int field_num = table_meta.field_num() - table_meta.sys_field_num();
  const int sys_field_num = table_meta.sys_field_num();
  for (auto& vs : inserts.values) {
    const Value *values = vs.data();
    const int value_num = static_cast<int>(vs.size());
    if (field_num != value_num) {
      LOG_WARN("schema mismatch. value num=%d, field num in schema=%d", value_num, field_num);
      return RC::SCHEMA_FIELD_MISSING;
    }

    // check fields type
    for (int i = 0; i < value_num; i++) {
      const FieldMeta *field_meta = table_meta.field(i + sys_field_num);
      const AttrType field_type = field_meta->type();
      const AttrType value_type = values[i].attr_type();
      if (value_type == NULLS && field_meta->nullable()) {
        continue;
      }
      if (field_type != value_type) {  // TODO try to convert the value type to field type
        if (TEXTS == field_type && CHARS == value_type) {
          if (MAX_TEXT_LENGTH < values[i].length()) {
            LOG_WARN("Text length:%d, over max_length 65535", values[i].length());
            return RC::INVALID_ARGUMENT;
          }
        } else {
          LOG_WARN("field type mismatch. table=%s, field=%s, field type=%d, value_type=%d",
            table_name, field_meta->name(), field_type, value_type);
          return RC::SCHEMA_FIELD_TYPE_MISMATCH;
        }
      }
      if(field_type == CHARS && values[i].length() > field_meta->len()){
          return RC::INVALID_ARGUMENT;
      }
      if(field_type == CHARS) {
        if (values[i].length() > field_meta->len()) {
          return RC::INVALID_ARGUMENT;
        }
        // 将不确定长度的 char 改为固定长度的 char
        char *char_value = (char*)malloc(field_meta->len());
        memset(char_value, 0, field_meta->len());
        memcpy(char_value, values[i].data(), values[i].length());
        const_cast<Value*>(values)[i].set_data(char_value, field_meta->len());
        free(char_value);
      }
    }
    valuess.emplace_back(values);
    value_nums.emplace_back(value_num);
  }

  // everything alright
  stmt = new InsertStmt(table, valuess, value_nums);
  return RC::SUCCESS;
}