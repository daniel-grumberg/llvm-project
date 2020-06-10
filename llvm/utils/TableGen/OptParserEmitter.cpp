//===- OptParserEmitter.cpp - Table Driven Command Line Parsing -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OptEmitter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cctype>
#include <cstring>
#include <map>

using namespace llvm;

static const std::string getOptionName(const Record &R) {
  // Use the record name unless EnumName is defined.
  if (isa<UnsetInit>(R.getValueInit("EnumName")))
    return std::string(R.getName());

  return std::string(R.getValueAsString("EnumName"));
}

static raw_ostream &write_cstring(raw_ostream &OS, llvm::StringRef Str) {
  OS << '"';
  OS.write_escaped(Str);
  OS << '"';
  return OS;
}

static void emitMarshallingInfoFlag(raw_ostream &OS, const Record &R) {
  OS << R.getValueAsBit("IsPositive");
}

static void emitMarshallingInfoString(raw_ostream &OS, const Record &R) {
  OS << R.getValueAsString("Normalizer");
  OS << ", ";
  OS << R.getValueAsString("Denormalizer");
}

static void emitScopedNormalizedValue(raw_ostream &OS,
                                      StringRef NormalizedValuesScope,
                                      StringRef NormalizedValue) {
  if (!NormalizedValuesScope.empty())
    OS << NormalizedValuesScope << "::";
  OS << NormalizedValue;
}

static void emitValueTable(raw_ostream &OS, StringRef OptionID,
                           StringRef Values, StringRef NormalizedValuesScope,
                           std::vector<StringRef> NormalizedValues) {
  SmallVector<StringRef, 8> SplitValues;
  Values.split(SplitValues, ',');
  assert(SplitValues.size() == NormalizedValues.size() &&
         "The number of associated definitions doesn't match the number of "
         "values");

  SmallString<64> MacroName("HANDLE_");
  MacroName += OptionID.upper();
  MacroName += "_VALUES";
  OS << "#ifdef " << MacroName << "\n";
  for (unsigned I = 0, E = SplitValues.size(); I != E; ++I) {
    OS << MacroName << "(\"" << SplitValues[I] << "\",";
    emitScopedNormalizedValue(OS, NormalizedValuesScope, NormalizedValues[I]);
    OS << ")\n";
  }
  OS << "#endif\n";
}

struct MarshallingKindInfo {
  const char *MacroName;
  void (*Emit)(raw_ostream &OS, const Record &R);
};

/// OptParserEmitter - This tablegen backend takes an input .td file
/// describing a list of options and emits a data structure for parsing and
/// working with those options when given an input command line.
namespace llvm {
void EmitOptParser(RecordKeeper &Records, raw_ostream &OS) {
  // Get the option groups and options.
  const std::vector<Record*> &Groups =
    Records.getAllDerivedDefinitions("OptionGroup");
  std::vector<Record*> Opts = Records.getAllDerivedDefinitions("Option");

  emitSourceFileHeader("Option Parsing Definitions", OS);

  array_pod_sort(Opts.begin(), Opts.end(), CompareOptionRecords);
  // Generate prefix groups.
  typedef SmallVector<SmallString<2>, 2> PrefixKeyT;
  typedef std::map<PrefixKeyT, std::string> PrefixesT;
  PrefixesT Prefixes;
  Prefixes.insert(std::make_pair(PrefixKeyT(), "prefix_0"));
  unsigned CurPrefix = 0;
  for (unsigned i = 0, e = Opts.size(); i != e; ++i) {
    const Record &R = *Opts[i];
    std::vector<StringRef> prf = R.getValueAsListOfStrings("Prefixes");
    PrefixKeyT prfkey(prf.begin(), prf.end());
    unsigned NewPrefix = CurPrefix + 1;
    if (Prefixes.insert(std::make_pair(prfkey, (Twine("prefix_") +
                                              Twine(NewPrefix)).str())).second)
      CurPrefix = NewPrefix;
  }

  // Dump prefixes.

  OS << "/////////\n";
  OS << "// Prefixes\n\n";
  OS << "#ifdef PREFIX\n";
  OS << "#define COMMA ,\n";
  for (PrefixesT::const_iterator I = Prefixes.begin(), E = Prefixes.end();
                                  I != E; ++I) {
    OS << "PREFIX(";

    // Prefix name.
    OS << I->second;

    // Prefix values.
    OS << ", {";
    for (PrefixKeyT::const_iterator PI = I->first.begin(),
                                    PE = I->first.end(); PI != PE; ++PI) {
      OS << "\"" << *PI << "\" COMMA ";
    }
    OS << "nullptr})\n";
  }
  OS << "#undef COMMA\n";
  OS << "#endif // PREFIX\n\n";

  OS << "/////////\n";
  OS << "// Groups\n\n";
  OS << "#ifdef OPTION\n";
  for (unsigned i = 0, e = Groups.size(); i != e; ++i) {
    const Record &R = *Groups[i];

    // Start a single option entry.
    OS << "OPTION(";

    // The option prefix;
    OS << "nullptr";

    // The option string.
    OS << ", \"" << R.getValueAsString("Name") << '"';

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", Group";

    // The containing option group (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The other option arguments (unused for groups).
    OS << ", INVALID, nullptr, 0, 0";

    // The option help text.
    if (!isa<UnsetInit>(R.getValueInit("HelpText"))) {
      OS << ",\n";
      OS << "       ";
      write_cstring(OS, R.getValueAsString("HelpText"));
    } else
      OS << ", nullptr";

    // The option meta-variable name (unused).
    OS << ", nullptr";

    // The option Values (unused for groups).
    OS << ", nullptr)\n";
  }
  OS << "\n";

  OS << "//////////\n";
  OS << "// Options\n\n";

  auto WriteOptRecordFields = [&](raw_ostream &OS, const Record &R) {
    // The option prefix;
    std::vector<StringRef> prf = R.getValueAsListOfStrings("Prefixes");
    OS << Prefixes[PrefixKeyT(prf.begin(), prf.end())] << ", ";

    // The option string.
    write_cstring(OS, R.getValueAsString("Name"));

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", " << R.getValueAsDef("Kind")->getValueAsString("Name");

    // The containing option group (if any).
    OS << ", ";
    const ListInit *GroupFlags = nullptr;
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group"))) {
      GroupFlags = DI->getDef()->getValueAsListInit("Flags");
      OS << getOptionName(*DI->getDef());
    } else
      OS << "INVALID";

    // The option alias (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Alias")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The option alias arguments (if any).
    // Emitted as a \0 separated list in a string, e.g. ["foo", "bar"]
    // would become "foo\0bar\0". Note that the compiler adds an implicit
    // terminating \0 at the end.
    OS << ", ";
    std::vector<StringRef> AliasArgs = R.getValueAsListOfStrings("AliasArgs");
    if (AliasArgs.size() == 0) {
      OS << "nullptr";
    } else {
      OS << "\"";
      for (size_t i = 0, e = AliasArgs.size(); i != e; ++i)
        OS << AliasArgs[i] << "\\0";
      OS << "\"";
    }

    // The option flags.
    OS << ", ";
    int NumFlags = 0;
    const ListInit *LI = R.getValueAsListInit("Flags");
    for (Init *I : *LI)
      OS << (NumFlags++ ? " | " : "") << cast<DefInit>(I)->getDef()->getName();
    if (GroupFlags) {
      for (Init *I : *GroupFlags)
        OS << (NumFlags++ ? " | " : "")
           << cast<DefInit>(I)->getDef()->getName();
    }
    if (NumFlags == 0)
      OS << '0';

    // The option parameter field.
    OS << ", " << R.getValueAsInt("NumArgs");

    // The option help text.
    if (!isa<UnsetInit>(R.getValueInit("HelpText"))) {
      OS << ",\n";
      OS << "       ";
      write_cstring(OS, R.getValueAsString("HelpText"));
    } else
      OS << ", nullptr";

    // The option meta-variable name.
    OS << ", ";
    if (!isa<UnsetInit>(R.getValueInit("MetaVarName")))
      write_cstring(OS, R.getValueAsString("MetaVarName"));
    else
      OS << "nullptr";

    // The option Values. Used for shell autocompletion.
    OS << ", ";
    if (!isa<UnsetInit>(R.getValueInit("Values")))
      write_cstring(OS, R.getValueAsString("Values"));
    else
      OS << "nullptr";
  };

  std::vector<const Record *> OptsWithMarshalling;
  for (unsigned I = 0, E = Opts.size(); I != E; ++I) {
    const Record &R = *Opts[I];

    // Start a single option entry.
    OS << "OPTION(";
    WriteOptRecordFields(OS, R);
    OS << ")\n";
    if (!isa<UnsetInit>(R.getValueInit("MarshallingKind")))
      OptsWithMarshalling.push_back(&R);
  }
  OS << "#endif // OPTION\n";

  for (unsigned I = 0, E = OptsWithMarshalling.size(); I != E; ++I) {
    const Record &R = *OptsWithMarshalling[I];
    assert(!isa<UnsetInit>(R.getValueInit("KeyPath")) &&
           !isa<UnsetInit>(R.getValueInit("DefaultValue")) &&
           "Must provide at least a key-path and a default value for emitting "
           "marshalling information");
    StringRef KindStr = R.getValueAsString("MarshallingKind");
    auto KindInfo = StringSwitch<MarshallingKindInfo>(KindStr)
                        .Case("flag", {"OPTION_WITH_MARSHALLING_FLAG",
                                       &emitMarshallingInfoFlag})
                        .Case("string", {"OPTION_WITH_MARSHALLING_STRING",
                                         &emitMarshallingInfoString})
                        .Default({"", nullptr});
    StringRef NormalizedValuesScope;
    if (!isa<UnsetInit>(R.getValueInit("NormalizedValuesScope")))
      NormalizedValuesScope = R.getValueAsString("NormalizedValuesScope");

    OS << "#ifdef " << KindInfo.MacroName << "\n";
    OS << KindInfo.MacroName << "(";
    WriteOptRecordFields(OS, R);
    OS << ", ";
    OS << R.getValueAsBit("ShouldAlwaysEmit");
    OS << ", ";
    OS << R.getValueAsString("KeyPath");
    OS << ", ";
    emitScopedNormalizedValue(OS, NormalizedValuesScope,
                              R.getValueAsString("DefaultValue"));
    OS << ",";
    KindInfo.Emit(OS, R);
    OS << ")\n";
    OS << "#endif\n";

    if (!isa<UnsetInit>(R.getValueInit("NormalizedValues"))) {
      assert(!isa<UnsetInit>(R.getValueInit("Values")) &&
             "Cannot provide associated definitions for value-less options");
      emitValueTable(OS, getOptionName(R), R.getValueAsString("Values"),
                     NormalizedValuesScope,
                     R.getValueAsListOfStrings("NormalizedValues"));
    }
  }

  OS << "\n";
  OS << "#ifdef OPTTABLE_ARG_INIT\n";
  OS << "//////////\n";
  OS << "// Option Values\n\n";
  for (unsigned I = 0, E = Opts.size(); I != E; ++I) {
    const Record &R = *Opts[I];
    if (isa<UnsetInit>(R.getValueInit("ValuesCode")))
      continue;
    OS << "{\n";
    OS << "bool ValuesWereAdded;\n";
    OS << R.getValueAsString("ValuesCode");
    OS << "\n";
    for (StringRef Prefix : R.getValueAsListOfStrings("Prefixes")) {
      OS << "ValuesWereAdded = Opt.addValues(";
      std::string S(Prefix);
      S += R.getValueAsString("Name");
      write_cstring(OS, S);
      OS << ", Values);\n";
      OS << "(void)ValuesWereAdded;\n";
      OS << "assert(ValuesWereAdded && \"Couldn't add values to "
            "OptTable!\");\n";
    }
    OS << "}\n";
  }
  OS << "\n";
  OS << "#endif // OPTTABLE_ARG_INIT\n";
}
} // end namespace llvm
