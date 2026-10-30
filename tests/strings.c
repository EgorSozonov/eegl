
//{{{ Utils

typedef struct {
    String name;
    String input;
    Compiler* expectedOutput;
} LexerTest;

typedef struct {
    String name;
    int totalTests;
    LexerTest* tests;
} LexerTestSet;


#define S   70000000 // A constant larger than the largest allowed file size. Separates parsed
                     // names from others


private Compiler* buildExpectedLexer(Arena *a, int totalTokens, Arr(Token) tokens) {
    Compiler* result = createLexer(empty, true, a);
    if (result == NULL) return result;

    CompResult* testRes = getCompResult(result);
    if (tokens == NULL) {
        return result;
    }
    for (int i = 0; i < totalTokens; i++) {
        Token tok = tokens[i];
        // offset nameIds and startBts for the standardText and standard nameIds correspondingly
        tok.startBt += testRes->stats.standardTextLen;
        if (tok.tp == tokWord || tok.tp == tokKey || tok.tp == tokType
             || (tok.tp == tokOperator && tok.pl2 == 10)
             || tok.tp == tokTypeVar || tok.tp == tokFieldAcc
        ) {
            if (tok.pl1 < S) { // parsed words
                tok.pl1 += testRes->stats.firstParsedName;
            } else { // built-in words
                tok.pl1 = nameOfStd(tok.pl1 - S);
            }
        }
        pushIntokens0(tok, result);
    }

    return result;
}

#define expect(toks) buildExpectedLexer(a, sizeof(toks)/sizeof(Token), toks)
#define expectEmpty(toks) buildExpectedLexer(a, 0, NULL)


private Compiler*
expectError0(Int errId, Arena *a, Int totalTokens, Arr(Token) tokens) {
   Compiler* result = buildExpectedLexer(a, totalTokens, tokens);
   setLexerError(errId, result);
   return result;
}

#define expectError(msg, toks) expectError0(msg, a, sizeof(toks)/sizeof(Token), toks)

private LexerTestSet* createTestSet0(String name, Arena *a, int count, Arr(LexerTest) tests) {
   LexerTestSet* result = allocateOnArena(sizeof(LexerTestSet), a);
   result->name = name;
   result->totalTests = count;
   result->tests = allocateOnArena(count*sizeof(LexerTest), a);
   if (result->tests == NULL)
      { return result; }
   for (int i = 0; i < count; i++) {
       result->tests[i] = tests[i];
   }
   return result;
}

#define createTestSet(n, a, tests) createTestSet0(n, a, sizeof(tests)/sizeof(LexerTest), tests)


void runLexerTest(LexerTest test, TestContext* ct) {
// Runs a single lexer test and prints err msg to stdout in case of failure. Returns error code
   ct->countTests++;
   if (ct->singleId > -1) {
      if (ct->countTests - 1 == ct->singleId)
         { ct->ranSingle = true; }
      else
         { return; }
   }
   Compiler* result = lexicallyAnalyze(test.input, ct->a);
   int equalityStatus = equalityLexer(result, test.expectedOutput);
   Int testId = ct->countTests - 1;
   if (equalityStatus == -2) {
      ct->countPassed += 1;
      return;
   } else if (equalityStatus == -1) {
      printf("\n\nERROR IN [%d][", testId);
      printStringNoLn(test.name);
      printf("]\nError msg:\n");
      CompResult* testRes = getCompResult(result);
      CompResult* expectedRes = getCompResult(test.expectedOutput);
      libeyr_printErrors(testRes);
      if (expectedRes->wasLexerError) {
         printf("\nBut was expected: ");
         libeyr_printErrors(expectedRes);
      } else {
          printf("\nBut was expected to be error-free\n");
      }
      printLexer(result);
   } else {
      printf("ERROR IN [%d][", testId);
      printStringNoLn(test.name);
      printf("]\nOn token %d\n", equalityStatus);
      printLexer(result);
   }
}

void runATestSet(LexerTestSet* (*testGenerator)(Arena*), TestContext* ct) {
    LexerTestSet* testSet = (testGenerator)(ct->a);
    for (int j = 0; j < testSet->totalTests; j++) {
        LexerTest test = testSet->tests[j];
        runLexerTest(test, ct);
    }
}

void
printTestResults(TestContext ct) {
   if (ct.countTests == 0) {
      printf("There were no tests to run!\n");
   } else if (ct.countPassed == ct.countTests) {
      if (ct.countTests > 1) {
         printf("Passed all %d tests!\n", ct.countTests);
      } else {
         printf("The test was passed.\n");
      }
   } else if (ct.singleId > -1) {
      if (ct.ranSingle) {
         printf(ct.countPassed == 1 ? "\nThe test was passed.\n" : "\nFailed the test!\n");
      } else {
         printf("Wanted to run test %d but encountered only %d tests\n",
            ct.singleId, ct.countTests
         );
      }
   } else {
      printf("Failed %d tests out of %d!\n", (ct.countTests - ct.countPassed), ct.countTests);
   }
}

//}}}


LexerTestSet* wordTests(Arena* a) {
    return createTestSet(s("Word lexer test"), a, ((LexerTest[]) {
        (LexerTest) {
            .name = s("Simple word lexing"),
            .input = s("asdf abc;"),
            .expectedOutput = expect(((Token[]){
                    (Token){ .tp = tokStmt, .pl2 = 2, .startBt = 0, .lenBts = 9 },
                    (Token){ .tp = tokWord, .pl1 = 0, .startBt = 0, .lenBts = 4 },
                    (Token){ .tp = tokWord, .pl1 = 1, .startBt = 5, .lenBts = 3 }
            }))
        },
	  };
}

int
main(int argc, char** argv) {
   printf("------------------\n");
   printf("--- LEXER TEST ---\n");
   printf("------------------\n");

   TestContext ct = (TestContext){.countTests = 0, .countPassed = 0,
       .singleId = -1, .ranSingle = false, .a = createArena() };

   if (argc == 2) {
      char* tail;
      long testId = strtol(argv[1], &tail, 10);
      if (*tail == '\0' && testId >= 0) {
         ct.singleId = (int)testId;
      }
   }

   runATestSet(&wordTests, &ct);
   runATestSet(&stringTests, &ct);
   runATestSet(&operatorTests, &ct);
   runATestSet(&punctuationTests, &ct);
   runATestSet(&numericTests, &ct);
   runATestSet(&coreFormTests, &ct);
   runATestSet(&typeTests, &ct);

   //runATestSet(&metaTests, &countPassed, &countTests, a);
   printTestResults(ct);

   deleteArena(ct.a);
   return getReturnCode(ct);
}
