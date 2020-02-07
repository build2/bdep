// file      : bdep/database-views.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_DATABASE_VIEWS_HXX
#define BDEP_DATABASE_VIEWS_HXX

#include <odb/core.hxx>

#include <bdep/types.hxx>
#include <bdep/utility.hxx>

namespace bdep
{
  #pragma db view table("sqlite_master")
  struct sqlite_master
  {
    string type;
    string name;
    string sql;

    #pragma db member(type) column("type")
    #pragma db member(name) column("type")
    #pragma db member(sql)  column("sql")
  };
}

#endif // BDEP_DATABASE_VIEWS_HXX
