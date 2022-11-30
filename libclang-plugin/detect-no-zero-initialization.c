#include <assert.h>
#include <clang-c/Index.h>
#include <stdio.h>

static enum CXChildVisitResult visitor(CXCursor cursor, CXCursor unused_cursor,
                                       CXClientData unused_data) {
  (void)unused_cursor;
  (void)unused_data;

  enum CXCursorKind kind = clang_getCursorKind(cursor);

  if (kind == CXCursor_VarDecl) {
    CXCursor init_cursor = clang_Cursor_getVarDeclInitializer(cursor);

    CXType type = clang_getCursorType(cursor);
    enum CX_StorageClass storage = clang_Cursor_getStorageClass(cursor);
    if (type.kind == CXType_Record && init_cursor.kind == CXCursor_CallExpr &&
        storage != CX_SC_Static && storage != CX_SC_Auto) {
      CXString type_pretty = clang_getTypeSpelling(type);
      CXString cursorName = clang_getCursorDisplayName(cursor);

      // Get the source location
      CXSourceRange range = clang_getCursorExtent(cursor);
      CXSourceLocation location = clang_getRangeStart(range);

      CXFile file;
      unsigned line;
      unsigned column;
      clang_getFileLocation(location, &file, &line, &column, NULL);

      CXString fileName = clang_getFileName(file);

      CXString pretty = clang_getCursorPrettyPrinted(cursor, NULL);
      printf("\x1b[31m%s:%d:%d:\x1b[0m %s\n", clang_getCString(fileName), line,
             column, clang_getCString(pretty));

      clang_disposeString(fileName);
      clang_disposeString(cursorName);
      clang_disposeString(type_pretty);
      clang_disposeString(pretty);
    }
  }

  return CXChildVisit_Recurse;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    return 1;
  }

  // Command line arguments required for parsing the TU
  const char *args[] = {"-std=c++11"};

  // Create an index with excludeDeclsFromPCH = 1, displayDiagnostics = 0
  CXIndex index = clang_createIndex(1, 0);

  CXTranslationUnit translationUnit = clang_parseTranslationUnit(
      index, argv[1], args, 1, NULL, 0, CXTranslationUnit_None);
  assert(translationUnit != NULL);

  // Visit all the nodes in the AST
  CXCursor cursor = clang_getTranslationUnitCursor(translationUnit);
  clang_visitChildren(cursor, visitor, 0);

  // Release memory
  clang_disposeTranslationUnit(translationUnit);
  clang_disposeIndex(index);

  return 0;
}
