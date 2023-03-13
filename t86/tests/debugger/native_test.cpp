#include <gtest/gtest.h>
#include <thread>
#include <string>
#include "debugger/Native.h"
#include "common/threads_messenger.h"
#include "t86/cpu.h"
#include "utils.h"

TEST_F(NativeTest, Basics) {
    auto program = R"(
.text

0 MOV R0, 1
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    ASSERT_TRUE(std::holds_alternative<ExecutionBegin>(native->WaitForDebugEvent()));
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, Reading) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();

    auto text = native->ReadText(0, 5);
    ASSERT_EQ(text.size(), 5);
    auto it = text.begin();
    ASSERT_EQ(*it++, "MOV R0, 3");
    ASSERT_EQ(*it++, "MOV R1, 2");
    ASSERT_EQ(*it++, "ADD R0, R1");
    ASSERT_EQ(*it++, "MOV R2, R0");
    ASSERT_EQ(*it++, "HALT");

    ASSERT_THROW({
        native->ReadText(0, 6);
    }, DebuggerError);
    ASSERT_THROW({
        native->ReadText(5, 1);
    }, DebuggerError);
    ASSERT_NO_THROW({
        native->ReadText(4, 1);
    });

    ASSERT_EQ(native->GetRegister("IP"), 0);
    ASSERT_EQ(native->GetRegister("R0"), 0);
    ASSERT_TRUE(std::holds_alternative<Singlestep>(native->PerformSingleStep()));
    ASSERT_EQ(native->GetRegister("IP"), 1);
    ASSERT_EQ(native->GetRegister("R0"), 3);
    
    ASSERT_THROW({
        native->GetRegister("R3"); 
    }, DebuggerError);

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, Writing) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    
    native->WriteText(0, {"MOV R2, 1", "MOV R1, 3"});
    auto text = native->ReadText(0, 2);
    ASSERT_EQ(text.size(), 2);
    ASSERT_EQ(text[0], "MOV R2, 1");
    ASSERT_EQ(text[1], "MOV R1, 3");
    ASSERT_THROW({
        native->WriteText(4, {"HALT", "HALT"});
    }, DebuggerError);
    ASSERT_THROW({
        native->WriteText(2, {"HALT 1"});
    }, DebuggerError);
    ASSERT_THROW({
        native->WriteText(1, {"MOV 1, R0 +"});
    }, DebuggerError);
    
    native->SetRegister("R0", 1);
    ASSERT_THROW({
        native->SetRegister("R3", 2);
    }, DebuggerError);
    ASSERT_EQ(native->GetRegister("R0"), 1);

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 4);
}

TEST_F(NativeTest, SimpleBreakpoint) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(2);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 2);
    ASSERT_EQ(native->GetRegister("R0"), 3);
    ASSERT_EQ(native->GetRegister("R1"), 2);

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    // Check that the ADD R0, R1 was executed even though it
    // was replaced by a breakpoint.
    ASSERT_EQ(native->GetRegister("R2"), 5);
}

TEST_F(NativeTest, StepOverBreakpoint) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(2);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 3);
    ASSERT_EQ(native->GetRegister("R1"), 2);

    ASSERT_TRUE(std::holds_alternative<Singlestep>(native->PerformSingleStep()));
    // Check that the ADD R0, R1 was executed even though it
    // was replaced by a breakpoint.
    ASSERT_EQ(native->GetRegister("R0"), 5);
    ASSERT_EQ(native->GetRegister("R2"), 0);
    native->ContinueExecution();
    native->WaitForDebugEvent();
    ASSERT_EQ(native->GetRegister("R0"), 5);
    ASSERT_EQ(native->GetRegister("R2"), 5);
}

TEST_F(NativeTest, BreakpointAtHaltSinglestep) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(4);

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->PerformSingleStep()));
}

TEST_F(NativeTest, BreakpointAtHaltContinue) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(4);

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, MultipleBreakpointHits) {
    auto program = R"(
.text

0 NOP
1 MOV BP, SP
2 SUB SP, 1
3 MOV [BP + -1], 0
4 JMP 14

# Check if iter variable is odd and print if so
5 MOV R0, [BP + -1]
6 AND R0, 1
7 CMP R0, 1
8 JNE 11

9 MOV R0, [BP + -1]
10 ADD R1, R0

# Increment iteration variable
11 MOV R0, [BP + -1]
12 ADD R0, 1
13 MOV [BP + -1], R0

# Condition check
14 MOV R0, [BP + -1]
15 CMP R0, 9
16 JLE 5

17 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(10);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 1);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 3);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 5);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 7);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 9);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, EnableDisableBreakpoints) {
    auto program = R"(
.text

0 NOP
1 MOV BP, SP
2 SUB SP, 1
3 MOV [BP + -1], 0
4 JMP 14

# Check if iter variable is odd and print if so
5 MOV R0, [BP + -1]
6 AND R0, 1
7 CMP R0, 1
8 JNE 11

9 MOV R0, [BP + -1]
10 ADD R1, R0

# Increment iteration variable
11 MOV R0, [BP + -1]
12 ADD R0, 1
13 MOV [BP + -1], R0

# Condition check
14 MOV R0, [BP + -1]
15 CMP R0, 9
16 JLE 5

17 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(10);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 1);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 3);
    native->SetBreakpoint(13);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 13);
    native->DisableSoftwareBreakpoint(13);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 5);
    ASSERT_EQ(native->GetRegister("IP"), 10);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 7);
    ASSERT_EQ(native->GetRegister("IP"), 10);
    native->DisableSoftwareBreakpoint(10);
    native->EnableSoftwareBreakpoint(13);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 13);
    native->DisableSoftwareBreakpoint(13);
    native->ContinueExecution();

    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R1"), 25);
}

TEST_F(NativeTest, BreakpointAtHalt) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(4);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, BreakpointSequence) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(1);
    native->SetBreakpoint(2);
    native->SetBreakpoint(3);
    native->SetBreakpoint(4);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 1);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 2);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 3);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 4);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("IP"), 5);
    ASSERT_EQ(native->GetRegister("R2"), 5);
}

TEST_F(NativeTest, PeekTextWithBreakpoints) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(1);
    auto text = native->ReadText(0, 3);
    ASSERT_EQ(text[0], "MOV R0, 3");
    ASSERT_EQ(text[1], "MOV R1, 2"); // Not BKPT!!
    ASSERT_EQ(text[2], "ADD R0, R1");

    native->DisableSoftwareBreakpoint(1);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
}

TEST_F(NativeTest, PokeTextWithBreakpoints) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();
    native->SetBreakpoint(1);
    native->WriteText(0, {
        "MOV R0, 3",
        "MOV R1, 2",
        "ADD R0, R1",
    });
    auto text = native->ReadText(0, 3);
    ASSERT_EQ(text[0], "MOV R0, 3");
    ASSERT_EQ(text[1], "MOV R1, 2");
    ASSERT_EQ(text[2], "ADD R0, R1");

    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<BreakpointHit>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 3);
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R1"), 2);
}

TEST_F(NativeTest, BreakpointsInvalid) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 ADD R0, R1
3 MOV R2, R0
4 HALT
)";
    Run(program, 3, 0);
    native->WaitForDebugEvent();

    ASSERT_THROW({native->EnableSoftwareBreakpoint(2);}, DebuggerError);
    ASSERT_THROW({native->DisableSoftwareBreakpoint(2);}, DebuggerError);
    ASSERT_THROW({native->UnsetBreakpoint(2);}, DebuggerError);
    native->SetBreakpoint(2);
    ASSERT_THROW({native->SetBreakpoint(2);}, DebuggerError);
    native->UnsetBreakpoint(2);
    ASSERT_THROW({native->UnsetBreakpoint(2);}, DebuggerError);
    ASSERT_THROW({native->EnableSoftwareBreakpoint(2);}, DebuggerError);
    ASSERT_THROW({native->DisableSoftwareBreakpoint(2);}, DebuggerError);
    
    native->ContinueExecution();
    ASSERT_TRUE(std::holds_alternative<ExecutionEnd>(native->WaitForDebugEvent()));
    ASSERT_EQ(native->GetRegister("R0"), 5);
    ASSERT_EQ(native->GetRegister("R1"), 2);
    ASSERT_EQ(native->GetRegister("R2"), 5);
}

TEST_F(NativeTest, GetFloatRegisters) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 MOV F0, 3.14
3 MOV F1, 5.9
4 MOV F2, F0
5 FADD F2, F1
6 NRW R2, F2
7 HALT
)";
    Run(program, 3, 3);
    native->WaitForDebugEvent();
    auto fregs = native->GetFloatRegisters();
    ASSERT_EQ(fregs.size(), 3);
    EXPECT_EQ(fregs.at("F0"), 0);
    EXPECT_EQ(fregs.at("F1"), 0);
    EXPECT_EQ(fregs.at("F2"), 0);
    native->ContinueExecution();
    native->WaitForDebugEvent();

    fregs = native->GetFloatRegisters();
    EXPECT_EQ(fregs.at("F0"), 3.14);
    EXPECT_EQ(fregs.at("F1"), 5.9);
    EXPECT_TRUE(fabs(fregs.at("F2") - (3.14 + 5.9)) < 0.0001);
    
    auto regs = native->GetRegisters();
    EXPECT_EQ(regs.at("R0"), 3);
    EXPECT_EQ(regs.at("R1"), 2);
    EXPECT_EQ(regs.at("R2"), 9);
}

TEST_F(NativeTest, SetFloatRegisters) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 MOV F0, 3.14
3 MOV F1, 5.9
4 MOV F2, F0
5 FADD F2, F1
6 NRW R2, F2
7 HALT
)";
    Run(program, 3, 3);
    native->WaitForDebugEvent();
    auto fregs = native->GetFloatRegisters();
    ASSERT_EQ(fregs.size(), 3);
    EXPECT_EQ(fregs.at("F0"), 0);
    EXPECT_EQ(fregs.at("F1"), 0);
    EXPECT_EQ(fregs.at("F2"), 0);
    native->SetFloatRegister("F2", 1.2);
    ASSERT_EQ(native->GetFloatRegister("F2"), 1.2);

    native->SetBreakpoint(5);
    native->ContinueExecution();
    native->WaitForDebugEvent();
    native->SetFloatRegisters({
        {"F1", 8.16},
    });
    ASSERT_EQ(native->GetFloatRegister("F1"), 8.16);
    native->ContinueExecution();
    native->WaitForDebugEvent();

    fregs = native->GetFloatRegisters();
    EXPECT_EQ(fregs.at("F0"), 3.14);
    EXPECT_EQ(fregs.at("F1"), 8.16);
    EXPECT_TRUE(fabs(fregs.at("F2") - (3.14 + 8.16)) < 0.0001);
    
    auto regs = native->GetRegisters();
    EXPECT_EQ(regs.at("R0"), 3);
    EXPECT_EQ(regs.at("R1"), 2);
    EXPECT_EQ(regs.at("R2"), 11);
}

TEST_F(NativeTest, InvalidFloatRegisters) {
    auto program = R"(
.text

0 MOV R0, 3
1 MOV R1, 2
2 MOV F0, 3.14
3 MOV F1, 5.9
4 MOV F2, F0
5 FADD F2, F1
6 NRW R2, F2
7 HALT
)";
    Run(program, 3, 3);
    native->WaitForDebugEvent();
    
    EXPECT_THROW({
        native->GetFloatRegister("R0");
    }, DebuggerError);
    EXPECT_THROW({
        native->GetFloatRegister("F3");
    }, DebuggerError);
    EXPECT_THROW({
        native->GetFloatRegister("0");
    }, DebuggerError);
    EXPECT_THROW({
        native->SetFloatRegisters({{"R0", 1.0}});
    }, DebuggerError);
    EXPECT_THROW({
        native->SetFloatRegisters({{"F3", 1.0}});
    }, DebuggerError);
    EXPECT_THROW({
        native->SetFloatRegisters({{"F0", 1.0}, {"F3", 2.0}});
    }, DebuggerError);
}
