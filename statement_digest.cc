/*
  Copyright (c) 2026 MariaDB plc
# Copyright (c) 2026 lefred

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.
*/

#define MYSQL_SERVER

#include <my_global.h>
#include <my_sys.h>
#include <sql_class.h>
#include <mysql/plugin_function.h>
#include <sql_digest.h>
#include <sql_digest_stream.h>
#include <sql_lex.h>
#include <sql_string.h>
#include <mysqld.h>

extern int MYSQLparse(THD *);
extern int ORAparse(THD *);

namespace
{

/**
  Temporarily replace parser-owned THD state.

  A function is evaluated while its containing statement still owns THD::lex
  and the active query arena.  Parsing the function argument therefore needs
  private instances of both.  All state is restored by the destructor.
*/
class Digest_parse_context
{
  THD *m_thd;
  MEM_ROOT m_mem_root;
  Query_arena m_arena;
  Query_arena m_backup_arena;
  LEX m_lex;
  LEX *m_saved_lex;
  Parser_state *m_saved_parser_state;
  sql_digest_state *m_saved_digest;
  const CHARSET_INFO *m_saved_client_charset;
  sql_digest_state m_digest;
  bool m_lex_started;

public:
  Digest_parse_context(THD *thd, uchar *token_buffer)
    : m_thd(thd),
      m_arena(&m_mem_root, Query_arena::STMT_CONVENTIONAL_EXECUTION),
      m_saved_lex(thd->lex),
      m_saved_parser_state(thd->m_parser_state),
      m_saved_digest(thd->m_digest),
      m_saved_client_charset(thd->variables.character_set_client),
      m_lex_started(false)
  {
    init_alloc_root(PSI_NOT_INSTRUMENTED, &m_mem_root, 4096, 0, MYF(0));
    thd->set_n_backup_active_arena(&m_arena, &m_backup_arena);
    thd->lex= &m_lex;
    lex_start(thd);
    m_lex_started= true;
    m_digest.reset(token_buffer, max_digest_length);
    thd->m_digest= &m_digest;
  }

  ~Digest_parse_context()
  {
    if (m_lex_started)
      lex_end(&m_lex);
    m_thd->lex= m_saved_lex;
    m_thd->m_parser_state= m_saved_parser_state;
    m_thd->m_digest= m_saved_digest;
    m_thd->variables.character_set_client= m_saved_client_charset;
    m_thd->update_charset();
    m_thd->restore_active_arena(&m_arena, &m_backup_arena);
    free_root(&m_mem_root, MYF(0));
  }

  sql_digest_storage *storage()
  {
    return &m_digest.m_digest_storage;
  }
};


bool parse_for_digest(THD *thd, String *statement,
                      sql_digest_storage **storage,
                      Digest_parse_context *context)
{
  /*
    Lex_input_stream requires a trailing NUL. c_ptr_safe() supplies it without
    changing the logical string length.
  */
  if (statement->length() && statement->ptr()[0] == '\0')
    statement->length(0);

  const CHARSET_INFO *cs= statement->charset();
  thd->variables.character_set_client= cs;
  thd->update_charset();

  Parser_state parser_state;
  if (parser_state.init(thd, const_cast<char *>(statement->c_ptr_safe()),
                        statement->length()))
    return true;

  parser_state.m_lip.multi_statements= false;
  parser_state.m_lip.m_digest= thd->m_digest;
  context->storage()->m_charset_number= cs->number;
  thd->m_parser_state= &parser_state;

  int parse_status= (thd->variables.sql_mode & MODE_ORACLE) ?
                    ORAparse(thd) : MYSQLparse(thd);
  *storage= context->storage();

  if (parse_status)
  {
    /*
      Mirrors parse_sql(): a syntax error inside a routine body (stored
      procedure/function/trigger/event/package) leaves thd->mem_root and
      thd->free_list repointed at the sp_head's own arena via
      sp_head::reset_thd_mem_root(). Without this call they stay that way
      until lex_end() below frees that sp_head, leaving thd->mem_root
      briefly dangling.
    */
    LEX::cleanup_lex_after_parse_error(thd);
  }

  return parse_status != 0;
}


class Item_func_statement_digest_base : public Item_str_func
{
protected:
  uchar *m_token_buffer;

  explicit Item_func_statement_digest_base(THD *thd, List<Item> &args)
    : Item_str_func(thd, args), m_token_buffer(nullptr)
  {}

  bool allocate_token_buffer(THD *thd)
  {
    if (!(m_token_buffer=
          reinterpret_cast<uchar *>(thd->alloc(max_digest_length))))
      return true;
    set_maybe_null();
    return false;
  }

public:
  Item *shallow_copy(THD *thd) const override= 0;
};


class Item_func_statement_digest final
  : public Item_func_statement_digest_base
{
  using Self= Item_func_statement_digest;

public:
  explicit Item_func_statement_digest(THD *thd, List<Item> &args)
    : Item_func_statement_digest_base(thd, args)
  {}

  bool fix_length_and_dec(THD *thd) override
  {
    max_length= DIGEST_HASH_SIZE * 2;
    collation.set(thd->variables.collation_connection,
                  DERIVATION_COERCIBLE);
    return allocate_token_buffer(thd);
  }

  String *val_str(String *result) override
  {
    StringBuffer<256> input;
    String *statement= args[0]->val_str(&input);
    if (!statement)
    {
      null_value= true;
      return nullptr;
    }

    uchar hash[DIGEST_HASH_SIZE];
    {
      THD *thd= current_thd;
      Digest_parse_context context(thd, m_token_buffer);
      sql_digest_storage *storage= nullptr;
      if (parse_for_digest(thd, statement, &storage, &context))
      {
        null_value= true;
        return nullptr;
      }
      compute_digest_hash(storage, hash);
    }

    static const char hex[]= "0123456789abcdef";
    if (result->alloc(DIGEST_HASH_SIZE * 2))
    {
      null_value= true;
      return nullptr;
    }
    char *output= const_cast<char *>(result->ptr());
    for (uint i= 0; i < DIGEST_HASH_SIZE; ++i)
    {
      output[i * 2]= hex[hash[i] >> 4];
      output[i * 2 + 1]= hex[hash[i] & 0x0f];
    }
    result->length(DIGEST_HASH_SIZE * 2);
    result->set_charset(collation.collation);
    null_value= false;
    return result;
  }

  LEX_CSTRING func_name_cstring() const override
  {
    static LEX_CSTRING name= "statement_digest"_LEX_CSTRING;
    return name;
  }

  Item *shallow_copy(THD *thd) const override
  {
    return get_item_copy<Self>(thd, this);
  }

  static Plugin_function *plugin_descriptor();
};


class Item_func_statement_digest_text final
  : public Item_func_statement_digest_base
{
  using Self= Item_func_statement_digest_text;

public:
  explicit Item_func_statement_digest_text(THD *thd, List<Item> &args)
    : Item_func_statement_digest_base(thd, args)
  {}

  bool fix_length_and_dec(THD *thd) override
  {
    max_length= MAX_BLOB_WIDTH;
    collation.set(args[0]->collation);
    return allocate_token_buffer(thd);
  }

  String *val_str(String *result) override
  {
    StringBuffer<256> input;
    String *statement= args[0]->val_str(&input);
    if (!statement)
    {
      null_value= true;
      return nullptr;
    }

    THD *thd= current_thd;
    Digest_parse_context context(thd, m_token_buffer);
    sql_digest_storage *storage= nullptr;
    if (parse_for_digest(thd, statement, &storage, &context))
    {
      null_value= true;
      return nullptr;
    }
    result->set_charset(statement->charset());
    compute_digest_text(storage, result);
    null_value= false;
    return result;
  }

  LEX_CSTRING func_name_cstring() const override
  {
    static LEX_CSTRING name= "statement_digest_text"_LEX_CSTRING;
    return name;
  }

  Item *shallow_copy(THD *thd) const override
  {
    return get_item_copy<Self>(thd, this);
  }

  static Plugin_function *plugin_descriptor();
};


template <class Item_type>
class Create_statement_digest_func final : public Create_native_func
{
public:
  Item *create_native(THD *thd, const LEX_CSTRING *name,
                      List<Item> *args) override
  {
    if (!args || args->elements != 1)
    {
      my_error(ER_WRONG_PARAMCOUNT_TO_NATIVE_FCT, MYF(0), name->str);
      return nullptr;
    }
    return new (thd->mem_root) Item_type(thd, *args);
  }
};


Plugin_function *Item_func_statement_digest::plugin_descriptor()
{
  static Create_statement_digest_func<Self> creator;
  static Plugin_function descriptor(&creator);
  return &descriptor;
}


Plugin_function *Item_func_statement_digest_text::plugin_descriptor()
{
  static Create_statement_digest_func<Self> creator;
  static Plugin_function descriptor(&creator);
  return &descriptor;
}

} // namespace


maria_declare_plugin(statement_digest)
{
  MariaDB_FUNCTION_PLUGIN,
  Item_func_statement_digest::plugin_descriptor(),
  "statement_digest",
  PLUGIN_AUTHOR,
  "Compute a Performance Schema compatible statement digest",
  PLUGIN_LICENSE,
  nullptr,
  nullptr,
  PLUGIN_HEX_VERSION,
  nullptr,
  nullptr,
  PLUGIN_VERSION,
  PLUGIN_MATURITY
},
{
  MariaDB_FUNCTION_PLUGIN,
  Item_func_statement_digest_text::plugin_descriptor(),
  "statement_digest_text",
  PLUGIN_AUTHOR,
  "Compute Performance Schema compatible normalized statement text",
  PLUGIN_LICENSE,
  nullptr,
  nullptr,
  PLUGIN_HEX_VERSION,
  nullptr,
  nullptr,
  PLUGIN_VERSION, 
  PLUGIN_MATURITY
}
maria_declare_plugin_end;
