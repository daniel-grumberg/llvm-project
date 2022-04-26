// RUN: rm -rf %t
// RUN: split-file %s %t

// Setup framework root
// RUN: mkdir -p %t/Frameworks/MyFramework.framework/Headers
// RUN: cp %t/MyFramework.h %t/Frameworks/MyFramework.framework/Headers/
// RUN: cp %t/MyHeader.h %t/Frameworks/MyFramework.framework/Headers/

// RUN: sed -e "s@SRCROOT@%{/t:regex_replacement}@g" \
// RUN: %t/reference.output.json.in >> %t/reference.output.json

// Headermap maps headers to the source root SRCROOT
// RUN: sed -e "s@SRCROOT@%{/t:regex_replacement}@g" \
// RUN: %t/headermap.hmap.json.in >> %t/headermap.hmap.json
// RUN: %hmaptool write %t/headermap.hmap.json %t/headermap.hmap

// Input headers use paths to the framework root/DSTROOT
// RUN: %clang -extract-api --product-name=MyFramework -target arm64-apple-macosx \
// RUN: -I%t/headermap.hmap -F%t/Frameworks \
// RUN: %t/Frameworks/MyFramework.framework/Headers/MyFramework.h \
// RUN: %t/Frameworks/MyFramework.framework/Headers/MyHeader.h \
// RUN: -o %t/output.json | FileCheck -allow-empty %s

// Generator version is not consistent across test runs, normalize it.
// RUN: sed -e "s@\"generator\": \".*\"@\"generator\": \"?\"@g" \
// RUN: %t/output.json >> %t/output-normalized.json
// RUN: diff %t/reference.output.json %t/output-normalized.json

// CHECK-NOT: error:
// CHECK-NOT: warning:

//--- headermap.hmap.json.in
{
  "mappings" :
    {
     "MyFramework/MyHeader.h" : "SRCROOT/MyHeader.h"
    }
}

//--- MyFramework.h
// Umbrella for MyFramework
#import <MyFramework/MyHeader.h>

//--- MyHeader.h
#import <OtherFramework/OtherHeader.h>
int MyInt;

//--- Frameworks/OtherFramework.framework/Headers/OtherHeader.h
int OtherInt;

//--- reference.output.json.in
{
  "metadata": {
    "formatVersion": {
      "major": 0,
      "minor": 5,
      "patch": 3
    },
    "generator": "?"
  },
  "module": {
    "name": "MyFramework",
    "platform": {
      "architecture": "arm64",
      "operatingSystem": {
        "minimumVersion": {
          "major": 11,
          "minor": 0,
          "patch": 0
        },
        "name": "macosx"
      },
      "vendor": "apple"
    }
  },
  "relationships": [],
  "symbols": [
    {
      "accessLevel": "public",
      "declarationFragments": [
        {
          "kind": "typeIdentifier",
          "preciseIdentifier": "c:I",
          "spelling": "int"
        },
        {
          "kind": "text",
          "spelling": " "
        },
        {
          "kind": "identifier",
          "spelling": "MyInt"
        }
      ],
      "identifier": {
        "interfaceLanguage": "c",
        "precise": "c:@MyInt"
      },
      "kind": {
        "displayName": "Global Variable",
        "identifier": "c.var"
      },
      "location": {
        "position": {
          "character": 5,
          "line": 2
        },
        "uri": "file://SRCROOT/MyHeader.h"
      },
      "names": {
        "navigator": [
          {
            "kind": "identifier",
            "spelling": "MyInt"
          }
        ],
        "subHeading": [
          {
            "kind": "identifier",
            "spelling": "MyInt"
          }
        ],
        "title": "MyInt"
      },
      "pathComponents": [
        "MyInt"
      ]
    }
  ]
}
