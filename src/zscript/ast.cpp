#include "dobject.h"
#include "sc_man.h"
#include "memarena.h"
#include "zcc_parser.h"
#include "zcc-parse.h"

class FLispString;
extern void (* const TreeNodePrinter[NUM_AST_NODE_TYPES])(FLispString &, ZCC_TreeNode *);

static const char *BuiltInTypeNames[] =
{
	"sint8", "uint8",
	"sint16", "uint16",
	"sint32", "uint32",
	"intauto",

	"bool",
	"float32", "float64", "floatauto",
	"string",
	"vector2",
	"vector3",
	"vector4",
	"name",
	"usertype"
};

class FLispString
{
public:
	operator FString &() { return Str; }

	FLispString()
	{
		NestDepth = Column = 0;
		WrapWidth = 72;
		NeedSpace = false;
	}

	void Open(const char *label)
	{
		size_t labellen = label != NULL ? strlen(label) : 0;
		CheckWrap(labellen + 1 + NeedSpace);
		if (NeedSpace)
		{
			Str << ' ';
		}
		Str << '(';
		if (label != NULL)
		{
			Str.AppendCStrPart(label, labellen);
		}
		Column += labellen + 1 + NeedSpace;
		NestDepth++;
		NeedSpace = (label != NULL);
	}
	void Close()
	{
		assert(NestDepth != 0);
		Str << ')';
		Column++;
		NestDepth--;
		NeedSpace = true;
	}
	void Break()
	{
		// Don't break if not needed.
		if (Column != NestDepth)
		{
			Str << '\n';
			Column = NestDepth;
			NeedSpace = false;
			if (NestDepth > 0)
			{
				Str.AppendFormat("%*s", NestDepth, "");
			}
		}
	}
	bool CheckWrap(size_t len)
	{
		if (len + Column > WrapWidth)
		{
			Break();
			return true;
		}
		return false;
	}
	void Add(const char *str, size_t len)
	{
		CheckWrap(len + NeedSpace);
		if (NeedSpace)
		{
			Str << ' ';
		}
		Str.AppendCStrPart(str, len);
		Column += len + NeedSpace;
		NeedSpace = true;
	}
	void Add(const char *str)
	{
		Add(str, strlen(str));
	}
	void Add(FString &str)
	{
		Add(str.GetChars(), str.Len());
	}
	void AddName(FName name)
	{
		size_t namelen = strlen(name.GetChars());
		CheckWrap(namelen + 2 + NeedSpace);
		if (NeedSpace)
		{
			NeedSpace = false;
			Str << ' ';
		}
		Str << '\'' << name.GetChars() << '\'';
		Column += namelen + 2 + NeedSpace;
		NeedSpace = true;
	}
	void AddChar(char c)
	{
		Add(&c, 1);
	}
	void AddInt(int i)
	{
		char buf[16];
		size_t len = mysnprintf(buf, countof(buf), "%d", i);
		Add(buf, len);
	}
	void AddHex(unsigned x)
	{
		char buf[10];
		size_t len = mysnprintf(buf, countof(buf), "%08x", x);
		Add(buf, len);
	}
	void AddFloat(double f)
	{
		char buf[32];
		size_t len = mysnprintf(buf, countof(buf), "%g", f);
		Add(buf, len);
	}
private:
	FString Str;
	size_t NestDepth;
	size_t Column;
	size_t WrapWidth;
	bool NeedSpace;
};

static void PrintNode(FLispString &out, ZCC_TreeNode *node)
{
	assert(TreeNodePrinter[NUM_AST_NODE_TYPES-1] != NULL);
	if (node->NodeType >= 0 && node->NodeType < NUM_AST_NODE_TYPES)
	{
		TreeNodePrinter[node->NodeType](out, node);
	}
	else
	{
		out.Open("unknown-node-type");
		out.AddInt(node->NodeType);
		out.Close();
	}
}

static void PrintNodes(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_TreeNode *p;

	if (node == NULL)
	{
		out.Add("nil", 3);
	}
	else
	{
		out.Open(NULL);
		p = node;
		do
		{
			PrintNode(out, p);
			p = p->SiblingNext;
		} while (p != node);
		out.Close();
	}
}

static void PrintBuiltInType(FLispString &out, EZCCBuiltinType type)
{
	assert(ZCC_NUM_BUILT_IN_TYPES == countof(BuiltInTypeNames));
	if (unsigned(type) >= unsigned(ZCC_NUM_BUILT_IN_TYPES))
	{
		char buf[30];
		size_t len = mysnprintf(buf, countof(buf), "bad-type-%u", type);
		out.Add(buf, len);
	}
	else
	{
		out.Add(BuiltInTypeNames[type]);
	}
}

static void PrintIdentifier(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Identifier *inode = (ZCC_Identifier *)node;
	out.Open("identifier");
	out.AddName(inode->Id);
	out.Close();
}

static void PrintStringConst(FLispString &out, FString str)
{
	FString outstr;
	outstr << '"';
	for (size_t i = 0; i < str.Len(); ++i)
	{
		if (str[i] == '"')
		{
			outstr << "\"";
		}
		else if (str[i] == '\\')
		{
			outstr << "\\\\";
		}
		else if (str[i] >= 32)
		{
			outstr << str[i];
		}
		else
		{
			outstr.AppendFormat("\\x%02X", str[i]);
		}
	}
	out.Add(outstr);
}

static void PrintClass(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Class *cnode = (ZCC_Class *)node;
	out.Open("class");
	PrintNodes(out, cnode->ClassName);
	PrintNodes(out, cnode->ParentName);
	PrintNodes(out, cnode->Replaces);
	out.AddHex(cnode->Flags);
	PrintNodes(out, cnode->Body);
	out.Close();
}

static void PrintStruct(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Struct *snode = (ZCC_Struct *)node;
	out.Break();
	out.Open("struct");
	out.AddName(snode->StructName);
	PrintNodes(out, snode->Body);
	out.Close();
}

static void PrintEnum(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Enum *enode = (ZCC_Enum *)node;
	out.Break();
	out.Open("enum");
	out.AddName(enode->EnumName);
	PrintBuiltInType(out, enode->EnumType);
	PrintNodes(out, enode->Elements);
	out.Close();
}

static void PrintEnumNode(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_EnumNode *enode = (ZCC_EnumNode *)node;
	out.Open("enum-node");
	out.AddName(enode->ElemName);
	PrintNodes(out, enode->ElemValue);
	out.Close();
}

static void PrintStates(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_States *snode = (ZCC_States *)node;
	out.Break();
	out.Open("states");
	PrintNodes(out, snode->Body);
	out.Close();
}

static void PrintStatePart(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-part");
	out.Close();
}

static void PrintStateLabel(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateLabel *snode = (ZCC_StateLabel *)node;
	out.Break();
	out.Open("state-label");
	out.AddName(snode->Label);
	out.Close();
}

static void PrintStateStop(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-stop");
	out.Close();
}

static void PrintStateWait(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-wait");
	out.Close();
}

static void PrintStateFail(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-fail");
	out.Close();
}

static void PrintStateLoop(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("state-loop");
	out.Close();
}

static void PrintStateGoto(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateGoto *snode = (ZCC_StateGoto *)node;
	out.Open("state-goto");
	PrintNodes(out, snode->Label);
	PrintNodes(out, snode->Offset);
	out.Close();
}

static void PrintStateLine(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_StateLine *snode = (ZCC_StateLine *)node;
	out.Break();
	out.Open("state-line");
	out.Add(snode->Sprite, 4);
	if (snode->bBright)
	{
		out.Add("bright", 6);
	}
	out.Add(*(snode->Frames));
	PrintNodes(out, snode->Offset);
	PrintNodes(out, snode->Action);
	out.Close();
}

static void PrintVarName(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_VarName *vnode = (ZCC_VarName *)node;
	out.Open("var-name");
	out.AddName(vnode->Name);
	out.Close();
}

static void PrintType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Type *tnode = (ZCC_Type *)node;
	out.Open("bad-type");
	PrintNodes(out, tnode->ArraySize);
	out.Close();
}

static void PrintBasicType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_BasicType *tnode = (ZCC_BasicType *)node;
	out.Open("basic-type");
	PrintNodes(out, tnode->ArraySize);
	PrintBuiltInType(out, tnode->Type);
	PrintNodes(out, tnode->UserType);
	out.Close();
}

static void PrintMapType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_MapType *tnode = (ZCC_MapType *)node;
	out.Open("map-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->KeyType);
	PrintNodes(out, tnode->ValueType);
	out.Close();
}

static void PrintDynArrayType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_DynArrayType *tnode = (ZCC_DynArrayType *)node;
	out.Open("dyn-array-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->ElementType);
	out.Close();
}

static void PrintClassType(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ClassType *tnode = (ZCC_ClassType *)node;
	out.Open("class-type");
	PrintNodes(out, tnode->ArraySize);
	PrintNodes(out, tnode->Restriction);
	out.Close();
}

static void OpenExprType(FLispString &out, EZCCExprType type)
{
	static const char *const types[] =
	{
		"nil",
		"id",
		"super",
		"self",
		"string-const",
		"int-const",
		"uint-const",
		"float-const",
		"func-call",
		"array-access",
		"member-access",
		"post-inc",
		"post-dec",
		"pre-inc",
		"pre-dec",
		"negate",
		"anti-negate",
		"bit-not",
		"bool-not",
		"size-of",
		"align-of",
		"add",
		"sub",
		"mul",
		"div",
		"mod",
		"pow",
		"cross-product",
		"dot-product",
		"left-shift",
		"right-shift",
		"concat",
		"lt",
		"gt",
		"lteq",
		"gteq",
		"ltgteq",
		"is",
		"eqeq",
		"neq",
		"apreq",
		"bit-and",
		"bit-or",
		"bit-xor",
		"bool-and",
		"bool-or",
		"scope",
		"trinary",
	};
	assert(countof(types) == PEX_COUNT_OF);

	char buf[32];

	if (unsigned(type) < countof(types))
	{
		mysnprintf(buf, countof(buf), "expr-%s", types[type]);
	}
	else
	{
		mysnprintf(buf, countof(buf), "bad-pex-%u", type);
	}
	out.Open(buf);
}

static void PrintExpression(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Expression *enode = (ZCC_Expression *)node;
	OpenExprType(out, enode->Operation);
	out.Close();
}

static void PrintExprID(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprID *enode = (ZCC_ExprID *)node;
	assert(enode->Operation == PEX_ID);
	out.Open("expr-id");
	out.AddName(enode->Identifier);
	out.Close();
}

static void PrintExprString(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprString *enode = (ZCC_ExprString *)node;
	assert(enode->Operation == PEX_StringConst);
	out.Open("expr-string-const");
	PrintStringConst(out, *enode->Value);
	out.Close();
}

static void PrintExprInt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprInt *enode = (ZCC_ExprInt *)node;
	assert(enode->Operation == PEX_IntConst || enode->Operation == PEX_UIntConst);
	OpenExprType(out, enode->Operation);
	out.AddInt(enode->Value);
	out.Close();
}

static void PrintExprFloat(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprFloat *enode = (ZCC_ExprFloat *)node;
	assert(enode->Operation == PEX_FloatConst);
	out.Open("expr-float-const");
	out.AddFloat(enode->Value);
	out.Close();
}

static void PrintExprFuncCall(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprFuncCall *enode = (ZCC_ExprFuncCall *)node;
	assert(enode->Operation == PEX_FuncCall);
	out.Open("expr-func-call");
	PrintNodes(out, enode->Function);
	PrintNodes(out, enode->Parameters);
	out.Close();
}

static void PrintExprMemberAccess(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprMemberAccess *enode = (ZCC_ExprMemberAccess *)node;
	assert(enode->Operation == PEX_MemberAccess);
	out.Open("expr-member-access");
	PrintNodes(out, enode->Left);
	out.AddName(enode->Right);
	out.Close();
}

static void PrintExprUnary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprUnary *enode = (ZCC_ExprUnary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Operand);
	out.Close();
}

static void PrintExprBinary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprBinary *enode = (ZCC_ExprBinary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Left);
	PrintNodes(out, enode->Right);
	out.Close();
}

static void PrintExprTrinary(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExprTrinary *enode = (ZCC_ExprTrinary *)node;
	OpenExprType(out, enode->Operation);
	PrintNodes(out, enode->Test);
	PrintNodes(out, enode->Left);
	PrintNodes(out, enode->Right);
	out.Close();
}

static void PrintFuncParam(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncParm *pnode = (ZCC_FuncParm *)node;
	out.Open("func-parm");
	out.AddName(pnode->Label);
	PrintNodes(out, pnode->Value);
	out.Close();
}

static void PrintStatement(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("statement");
	out.Close();
}

static void PrintCompoundStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_CompoundStmt *snode = (ZCC_CompoundStmt *)node;
	out.Open("compound-stmt");
	PrintNodes(out, snode->Content);
	out.Close();
}

static void PrintContinueStmt(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("continue-stmt");
	out.Close();
}

static void PrintBreakStmt(FLispString &out, ZCC_TreeNode *node)
{
	out.Open("break-stmt");
	out.Close();
}

static void PrintReturnStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ReturnStmt *snode = (ZCC_ReturnStmt *)node;
	out.Open("return-stmt");
	PrintNodes(out, snode->Values);
	out.Close();
}

static void PrintExpressionStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ExpressionStmt *snode = (ZCC_ExpressionStmt *)node;
	out.Open("expression-stmt");
	PrintNodes(out, snode->Expression);
	out.Close();
}

static void PrintIterationStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_IterationStmt *snode = (ZCC_IterationStmt *)node;
	out.Open("iteration-stmt");
	out.Add((snode->CheckAt == ZCC_IterationStmt::Start) ? "start" : "end");
	PrintNodes(out, snode->LoopCondition);
	PrintNodes(out, snode->LoopBumper);
	PrintNodes(out, snode->LoopStatement);
	out.Close();
}

static void PrintIfStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_IfStmt *snode = (ZCC_IfStmt *)node;
	out.Open("if-stmt");
	PrintNodes(out, snode->Condition);
	PrintNodes(out, snode->TruePath);
	PrintNodes(out, snode->FalsePath);
	out.Close();
}

static void PrintSwitchStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_SwitchStmt *snode = (ZCC_SwitchStmt *)node;
	out.Open("switch-stmt");
	PrintNodes(out, snode->Condition);
	PrintNodes(out, snode->Content);
	out.Close();
}

static void PrintCaseStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_CaseStmt *snode = (ZCC_CaseStmt *)node;
	out.Open("case-stmt");
	PrintNodes(out, snode->Condition);
	out.Close();
}

static void BadAssignOp(FLispString &out, int op)
{
	char buf[32];
	size_t len = mysnprintf(buf, countof(buf), "assign-op-%d", op);
	out.Add(buf, len);
}

static void PrintAssignStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_AssignStmt *snode = (ZCC_AssignStmt *)node;
	out.Open("assign-stmt");
	switch (snode->AssignOp)
	{
	case ZCC_EQ:		out.AddChar('='); break;
	case ZCC_MULEQ:		out.Add("*=", 2); break;
	case ZCC_DIVEQ:		out.Add("/=", 2); break;
	case ZCC_MODEQ:		out.Add("%=", 2); break;
	case ZCC_ADDEQ:		out.Add("+=", 2); break;
	case ZCC_SUBEQ:		out.Add("-=", 2); break;
	case ZCC_LSHEQ:		out.Add("<<=", 2); break;
	case ZCC_RSHEQ:		out.Add(">>=", 2); break;
	case ZCC_ANDEQ:		out.Add("&=", 2); break;
	case ZCC_OREQ:		out.Add("|=", 2); break;
	case ZCC_XOREQ:		out.Add("^=", 2); break;
	default:			BadAssignOp(out, snode->AssignOp); break;
	}
	PrintNodes(out, snode->Dests);
	PrintNodes(out, snode->Sources);
	out.Close();
}

static void PrintLocalVarStmt(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_LocalVarStmt *snode = (ZCC_LocalVarStmt *)node;
	out.Open("local-var-stmt");
	PrintNodes(out, snode->Type);
	PrintNodes(out, snode->Vars);
	PrintNodes(out, snode->Inits);
	out.Close();
}

static void PrintFuncParamDecl(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncParamDecl *dnode = (ZCC_FuncParamDecl *)node;
	out.Open("func-param-decl");
	PrintNodes(out, dnode->Type);
	out.AddName(dnode->Name);
	out.AddHex(dnode->Flags);
	out.Close();
}

static void PrintConstantDef(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_ConstantDef *dnode = (ZCC_ConstantDef *)node;
	out.Open("constant-def");
	out.AddName(dnode->Name);
	PrintNodes(out, dnode->Value);
	out.Close();
}

static void PrintDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_Declarator *dnode = (ZCC_Declarator *)node;
	out.Open("declarator");
	PrintNodes(out, dnode->Type);
	out.AddHex(dnode->Flags);
	out.Close();
}

static void PrintVarDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_VarDeclarator *dnode = (ZCC_VarDeclarator *)node;
	out.Break();
	out.Open("var-declarator");
	PrintNodes(out, dnode->Type);
	out.AddHex(dnode->Flags);
	PrintNodes(out, dnode->Names);
	out.Close();
}

static void PrintFuncDeclarator(FLispString &out, ZCC_TreeNode *node)
{
	ZCC_FuncDeclarator *dnode = (ZCC_FuncDeclarator *)node;
	out.Break();
	out.Open("func-declarator");
	PrintNodes(out, dnode->Type);
	out.AddHex(dnode->Flags);
	out.AddName(dnode->Name);
	PrintNodes(out, dnode->Params);
	PrintNodes(out, dnode->Body);
	out.Close();
}

void (* const TreeNodePrinter[NUM_AST_NODE_TYPES])(FLispString &, ZCC_TreeNode *) =
{
	PrintIdentifier,
	PrintClass,
	PrintStruct,
	PrintEnum,
	PrintEnumNode,
	PrintStates,
	PrintStatePart,
	PrintStateLabel,
	PrintStateStop,
	PrintStateWait,
	PrintStateFail,
	PrintStateLoop,
	PrintStateGoto,
	PrintStateLine,
	PrintVarName,
	PrintType,
	PrintBasicType,
	PrintMapType,
	PrintDynArrayType,
	PrintClassType,
	PrintExpression,
	PrintExprID,
	PrintExprString,
	PrintExprInt,
	PrintExprFloat,
	PrintExprFuncCall,
	PrintExprMemberAccess,
	PrintExprUnary,
	PrintExprBinary,
	PrintExprTrinary,
	PrintFuncParam,
	PrintStatement,
	PrintCompoundStmt,
	PrintContinueStmt,
	PrintBreakStmt,
	PrintReturnStmt,
	PrintExpressionStmt,
	PrintIterationStmt,
	PrintIfStmt,
	PrintSwitchStmt,
	PrintCaseStmt,
	PrintAssignStmt,
	PrintLocalVarStmt,
	PrintFuncParamDecl,
	PrintConstantDef,
	PrintDeclarator,
	PrintVarDeclarator,
	PrintFuncDeclarator
};

FString ZCC_PrintAST(ZCC_TreeNode *root)
{
	FLispString out;
	PrintNodes(out, root);
	return out;
}