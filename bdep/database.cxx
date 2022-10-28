// file      : bdep/database.cxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#include <bdep/database.hxx>

#include <odb/schema-catalog.hxx>
#include <odb/sqlite/exceptions.hxx>

#include <bdep/diagnostics.hxx>

#include <bdep/project.hxx>
#include <bdep/project-odb.hxx>
#include <bdep/database-views.hxx>
#include <bdep/database-views-odb.hxx>

using namespace std;

namespace bdep
{
  using namespace odb::sqlite;
  using odb::schema_catalog;

  // Register the data migration functions.
  //
#if 0
  template <odb::schema_version v>
  using migration_entry = odb::data_migration_entry<v, DB_SCHEMA_VERSION_BASE>;

  static const migration_entry<3>
  migrate_v3 ([] (odb::database& db)
  {
  });
#endif

  database
  open (const dir_path& d, tracer& tr, bool create)
  {
    tracer trace ("open");

    path f (d / bdep_file);

    if (exists (f))
      create = false;
    else if (!create)
      fail << d << " does not look like an initialized project directory" <<
        info << "run 'bdep init' to initialize";

    try
    {
      // We don't need the thread pool.
      //
      unique_ptr<connection_factory> cf (new single_connection_factory);

      database db (f.string (),
                   SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0),
                   true,                  // Enable FKs.
                   "",                    // Default VFS.
                   move (cf));

      db.tracer (trace);

      // Lock the database for as long as the connection is active. First we
      // set locking_mode to EXCLUSIVE which instructs SQLite not to release
      // any locks until the connection is closed. Then we force SQLite to
      // acquire the write lock by starting exclusive transaction. See the
      // locking_mode pragma documentation for details. This will also fail if
      // the database is inaccessible (e.g., file does not exist, already used
      // by another process, etc).
      //
      try
      {
        connection_ptr c (db.connection ());
        c->execute ("PRAGMA locking_mode = EXCLUSIVE");
        transaction t (c->begin_exclusive ());

        if (create)
        {
          // Create the new schema.
          //
          if (db.schema_version () != 0)
            fail << f << ": already has database schema";

          schema_catalog::create_schema (db);

          // Make path comparison case-insensitive for certain platforms.
          //
          // For details on this technique see the SQLite's ALTER TABLE
          // documentation. Note that here we don't increment SQLite's
          // schema_version since we are in the same transaction as where we
          // have created the schema.
          //
#ifdef _WIN32
          db.execute ("PRAGMA writable_schema = ON");

          const char* where ("type == 'table' AND name == 'configuration'");

          sqlite_master t (db.query_value<sqlite_master> (where));

          auto set_nocase = [&t] (const char* name)
          {
            string n ('\"' + string (name) + '\"');
            size_t p (t.sql.find (n));
            assert (p != string::npos);
            p = t.sql.find_first_of (",)", p);
            assert (p != string::npos);
            t.sql.insert (p, " COLLATE NOCASE");
          };

          set_nocase ("path");
          set_nocase ("relative_path");

          db.execute ("UPDATE sqlite_master"
                      " SET sql = '" + t.sql + "'"
                      " WHERE " + where);

          db.execute ("PRAGMA writable_schema = OFF");
          db.execute ("PRAGMA integrity_check");
#endif
        }
        else
        {
          // Migrate the database if necessary.
          //
          odb::schema_version sv  (db.schema_version ());
          odb::schema_version scv (schema_catalog::current_version (db));

          if (sv != scv)
          {
            if (sv < schema_catalog::base_version (db))
              fail << "project " << d << " is too old";

            if (sv > scv)
              fail << "project " << d << " is too new";

            schema_catalog::migrate (db);
          }
        }

        t.commit ();
      }
      catch (odb::timeout&)
      {
        fail << "project " << d << " is already used by another process";
      }

      db.tracer (tr); // Switch to the caller's tracer.
      return db;
    }
    catch (const database_exception& e)
    {
      fail << f << ": " << e.message () << endf;
    }
  }
}
