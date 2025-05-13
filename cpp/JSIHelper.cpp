//
//  JSIHelper.cpp
//  react-native-quick-sqlite
//
//  Created by Oscar on 13.03.22.
//

#include "JSIHelper.h"

#include <utility>

using namespace std;
using namespace facebook;

QuickValue createNullQuickValue()
{
  return QuickValue{
    .dataType = NULL_VALUE};
}

QuickValue createBooleanQuickValue(bool value)
{
  return QuickValue{
    .dataType = BOOLEAN,
    .booleanValue = int(value)};
}

QuickValue createTextQuickValue(string value)
{
  return QuickValue{
    .dataType = TEXT,
    .textValue = std::move(value)};
}

QuickValue createIntegerQuickValue(int value)
{
  return QuickValue{
    .dataType = INTEGER,
    .doubleOrIntValue = static_cast<double>(value)};
}

QuickValue createIntegerQuickValue(double value)
{
  return QuickValue{
    .dataType = INTEGER,
    .doubleOrIntValue = value};
}

QuickValue createInt64QuickValue(long long value)
{
  return QuickValue{
    .dataType = INT64,
    .int64Value = value};
}

QuickValue createDoubleQuickValue(double value)
{
  return QuickValue{
    .dataType = DOUBLE,
    .doubleOrIntValue = value};
}

QuickValue createArrayBufferQuickValueByCopying(const uint8_t *arrayBufferValue, size_t arrayBufferSize)
{
  vector<uint8_t> copy(arrayBufferValue, arrayBufferValue + arrayBufferSize);

  return QuickValue{
    .dataType = ARRAY_BUFFER,
    .arrayBuffer = copy};
}

void jsiQueryArgumentsToSequelParam(jsi::Runtime &rt, jsi::Value const &params, vector<QuickValue> *target)
{
  if (params.isNull() || params.isUndefined())
  {
    return;
  }

  jsi::Array values = params.asObject(rt).asArray(rt);

  for (int ii = 0; ii < values.length(rt); ii++)
  {

    jsi::Value value = values.getValueAtIndex(rt, ii);
    if (value.isNull() || value.isUndefined())
    {
      target->push_back(createNullQuickValue());
    }
    else if (value.isBool())
    {
      target->push_back(createBooleanQuickValue(value.getBool()));
    }
    else if (value.isNumber())
    {
      double doubleVal = value.asNumber();
      int intVal = (int)doubleVal;
      long long longVal = (long)doubleVal;
      if (intVal == doubleVal)
      {
        target->push_back(createIntegerQuickValue(intVal));
      }
      else if (longVal == doubleVal)
      {
        target->push_back(createInt64QuickValue(longVal));
      }
      else
      {
        target->push_back(createDoubleQuickValue(doubleVal));
      }
    }
    else if (value.isString())
    {
      string strVal = value.asString(rt).utf8(rt);
      target->push_back(createTextQuickValue(strVal));
    }
    else if (value.isObject())
    {
      auto obj = value.asObject(rt);
      if (obj.isArrayBuffer(rt))
      {
        auto buf = obj.getArrayBuffer(rt);
        target->push_back(createArrayBufferQuickValueByCopying(buf.data(rt), buf.size(rt)));
      }
    }
    else
    {
      target->push_back(createNullQuickValue());
    }
  }
}

jsi::Value createSequelQueryExecutionResult(jsi::Runtime &rt, SQLiteOPResult status, vector<map<string, QuickValue>> *results, vector<QuickColumnMetadata> *metadata)
{
  if(status.type == SQLiteError) {
    throw std::invalid_argument(status.errorMessage);
  }

  jsi::Object res = jsi::Object(rt);

  res.setProperty(rt, "rowsAffected", jsi::Value(status.rowsAffected));
  if (status.rowsAffected > 0 && status.insertId != 0)
  {
    res.setProperty(rt, "insertId", jsi::Value(status.insertId));
  }

  // Converting row results into objects
  size_t rowCount = results->size();
  jsi::Object rows = jsi::Object(rt);
  if (rowCount > 0 && metadata != NULL)
  {
    auto array = jsi::Array(rt, rowCount);
    for (int i = 0; i < rowCount; i++)
    {
      jsi::Object rowObject = jsi::Object(rt);
      auto row = results -> at(i);
      // Iterate over metadata to maintain column order
      for (const auto &column : *metadata)
      {
        std::string columnName = column.columnName;
        auto it = row.find(columnName);
        if (it != row.end())
        {
          QuickValue value = it -> second;
          if (value.dataType == TEXT)
          {
            // using value.textValue (std::string) directly allows jsi::String to use length property of std::string (allowing strings with NULLs in them like SQLite does)
            rowObject.setProperty(rt, columnName.c_str(), jsi::String::createFromUtf8(rt, value.textValue));
          } else if (value.dataType == INTEGER)
          {
            rowObject.setProperty(rt, columnName.c_str(), jsi::Value(value.doubleOrIntValue));
          } else if (value.dataType == DOUBLE)
          {
            rowObject.setProperty(rt, columnName.c_str(), jsi::Value(value.doubleOrIntValue));
          } else if (value.dataType == ARRAY_BUFFER)
          {
            jsi::Function array_buffer_ctor = rt.global().getPropertyAsFunction(rt, "ArrayBuffer");
            jsi::Object o = array_buffer_ctor.callAsConstructor(rt, (int) value.arrayBuffer.size()).getObject(rt);
            jsi::ArrayBuffer buf = o.getArrayBuffer(rt);
            // It's a shame we have to copy here: see https://github.com/facebook/hermes/pull/419 and https://github.com/facebook/hermes/issues/564.
            memcpy(buf.data(rt), value.arrayBuffer.data(), value.arrayBuffer.size());
            rowObject.setProperty(rt, columnName.c_str(), o);
          } else
          {
            rowObject.setProperty(rt, columnName.c_str(), jsi::Value(nullptr));
          }
        } else
        {
          rowObject.setProperty(rt, columnName.c_str(), jsi::Value(nullptr));
        }
      }
      array.setValueAtIndex(rt, i, move(rowObject));
    }
    rows.setProperty(rt, "_array", move(array));
    res.setProperty(rt, "rows", move(rows));
  }

  if(metadata != NULL)
  {
    size_t column_count = metadata->size();
    auto column_array = jsi::Array(rt, column_count);
    for (int i = 0; i < column_count; i++) {
      auto column = metadata->at(i);
      jsi::Object column_object = jsi::Object(rt);
      column_object.setProperty(rt, "columnName", jsi::String::createFromUtf8(rt, column.columnName.c_str()));
      column_object.setProperty(rt, "columnDeclaredType", jsi::String::createFromUtf8(rt, column.columnDeclaredType.c_str()));
      column_object.setProperty(rt, "columnIndex", jsi::Value(column.columnIndex));
      column_array.setValueAtIndex(rt, i, move(column_object));
    }
    res.setProperty(rt, "metadata", move(column_array));
  }
  rows.setProperty(rt, "length", jsi::Value((int)rowCount));

  return move(res);
}
