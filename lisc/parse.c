/* really crude parser
 */
#include <ctype.h>
#include <string.h>

#include "lisc.h"

enum {
	NSym = 256,
};

OpDesc opdesc[OLast] = {
	[OAdd] = { 2, 1, "add" },
	[OSub] = { 2, 0, "sub" },
	[ODiv] = { 2, 0, "div" },
	[OMod] = { 2, 0, "mod" },
	[OCopy] = { 1, 0, "copy" },
};

typedef enum {
	PXXX,
	PLbl,
	PPhi,
	PIns,
	PEnd,
} PState;

typedef enum {
	TXXX,
	TCopy,
	TAdd,
	TSub,
	TDiv,
	TMod,
	TPhi,
	TJmp,
	TJez,
	TRet,

	TNum,
	TVar,
	TLbl,
	TEq,
	TComma,
	TLParen,
	TRParen,
	TNL,
	TEOF,
} Token;


static FILE *inf;
static Token thead;
static struct {
	long long num;
	char *str;
} tokval;
static int lnum;

static Sym sym[NSym];
static int ntmp;
static Ins ins[NIns], *curi;
static Phi **plink;
static struct {
	char name[NString];
	Blk *blk;
} bmap[NBlk+1];
static Blk *curb;
static Blk **blink;
static int nblk;


void *
alloc(size_t n)
{
	void *p;

	p = calloc(1, n);
	if (!p)
		abort();
	return p;
}

static void
err(char *s)
{
	/* todo, export the error handling in util.c */
	fprintf(stderr, "parse error: %s (line %d)\n", s, lnum);
	exit(1);
}

static Token
lex()
{
	static struct {
		char *str;
		Token tok;
	} tmap[] = {
		{ "copy", TCopy },
		{ "add", TAdd },
		{ "sub", TSub },
		{ "div", TDiv },
		{ "mod", TMod },
		{ "phi", TPhi },
		{ "jmp", TJmp },
		{ "jez", TJez },
		{ "ret", TRet },
		{ 0 },
	};
	static char tok[NString];
	int c, i, sgn;
	Token t;

	do
		c = fgetc(inf);
	while (isblank(c));
	switch (c) {
	case EOF:
		return TEOF;
	case ',':
		return TComma;
	case '(':
		return TLParen;
	case ')':
		return TRParen;
	case '=':
		return TEq;
	case '%':
		t = TVar;
		c = fgetc(inf);
		goto Alpha;
	case '@':
		t = TLbl;
		c = fgetc(inf);
		goto Alpha;
	case '#':
		while (fgetc(inf) != '\n')
			;
	case '\n':
		lnum++;
		return TNL;
	}
	if (isdigit(c) || c == '-') {
		if (c == '-') {
			tokval.num = 0;
			sgn = -1;
		} else {
			tokval.num = c - '0';
			sgn = 1;
		}
		for (;;) {
			c = fgetc(inf);
			if (!isdigit(c))
				break;
			tokval.num *= 10;
			tokval.num += c - '0';
		}
		ungetc(c, inf);
		tokval.num *= sgn;
		return TNum;
	}
	t = TXXX;
Alpha:
	if (!isalpha(c))
		err("lexing failure");
	i = 0;
	do {
		if (i >= NString-1)
			err("identifier too long");
		tok[i++] = c;
		c = fgetc(inf);
	} while (isalpha(c) || isdigit(c));
	tok[i] = 0;
	ungetc(c, inf);
	if (t != TXXX) {
		tokval.str = tok;
		return t;
	}
	for (i=0; tmap[i].str; i++)
		if (strcmp(tok, tmap[i].str) == 0)
			return tmap[i].tok;
	err("unknown keyword");
	return -1;
}

static Token
peek()
{
	if (thead == TXXX)
		thead = lex();
	return thead;
}

static Token
next()
{
	Token t;

	t = peek();
	thead = TXXX;
	return t;
}

static Blk *
blocka()
{
	static Blk zblock;
	Blk *b;

	b = alloc(sizeof *b);
	*b = zblock;
	b->id = nblk++;
	return b;
}

static Ref
varref(char *v)
{
	int t;

	for (t=Tmp0; t<ntmp; t++)
		if (sym[t].type == STmp)
		if (strcmp(v, sym[t].name) == 0)
			return SYM(t);
	if (t >= NSym)
		err("too many symbols");
	sym[t].type = STmp;
	strcpy(sym[t].name, v);
	sym[t].blk = 0;
	ntmp++;
	return SYM(t);
}

static Ref
parseref()
{
	switch (next()) {
	case TVar:
		return varref(tokval.str);
	case TNum:
		if (tokval.num > NRef || tokval.num < 0)
			/* todo, use constants table */
			abort();
		return CONST(tokval.num);
	default:
		return R;
	}
}

static Blk *
findblk(char *name)
{
	int i;

	assert(name[0]);
	for (i=0; i<NBlk; i++)
		if (!bmap[i].blk || strcmp(bmap[i].name, name) == 0)
			break;
	if (i == NBlk)
		err("too many blocks");
	if (!bmap[i].blk) {
		assert(bmap[i].name[0] == 0);
		strcpy(bmap[i].name, name);
		bmap[i].blk = blocka();
		strcpy(bmap[i].blk->name, name);
	}
	return bmap[i].blk;
}

static void
expect(Token t)
{
	static char *names[] = {
		[TLbl] = "label",
		[TComma] = "comma",
		[TEq] = "=",
		[TNL] = "newline",
		[TEOF] = 0,
	};
	char buf[128], *s1, *s2;
	Token t1;

	t1 = next();
	if (t == t1)
		return;
	s1 = names[t] ? names[t] : "??";
	s2 = names[t1] ? names[t1] : "??";
	snprintf(buf, sizeof buf,
		"%s expected (got %s instead)", s1, s2);
	err(buf);
}

static void
closeblk()
{
	curb->nins = curi - ins;
	curb->ins = alloc(curb->nins * sizeof(Ins));
	memcpy(curb->ins, ins, curb->nins * sizeof(Ins));
	blink = &curb->link;
	curi = ins;
}

static PState
parseline(PState ps)
{
	Ref arg[NPred] = {0};
	Blk *blk[NPred];
	Phi *phi;
	Ref r;
	Token t;
	Blk *b;
	int op, i;

	assert(arg[0] == R);
	do
		t = next();
	while (t == TNL);
	if (ps == PLbl && t != TLbl && t != TEOF)
		err("label or end of file expected");
	switch (t) {
	default:
		err("label, instruction or jump expected");
	case TEOF:
		return PEnd;
	case TVar:
		break;
	case TLbl:
		b = findblk(tokval.str);
		if (b->jmp.type != JXXX)
			err("multiple definitions of block");
		if (curb && curb->jmp.type == JXXX) {
			closeblk();
			curb->jmp.type = JJmp;
			curb->s1 = b;
		}
		*blink = b;
		curb = b;
		plink = &curb->phi;
		expect(TNL);
		return PPhi;
	case TRet:
		curb->jmp.type = JRet;
		goto Close;
	case TJmp:
		curb->jmp.type = JJmp;
		goto Jump;
	case TJez:
		curb->jmp.type = JJez;
		r = parseref();
		if (r == R)
			err("invalid argument for jez jump");
		curb->jmp.arg = r;
		expect(TComma);
	Jump:
		expect(TLbl);
		curb->s1 = findblk(tokval.str);
		if (curb->jmp.type != JJmp) {
			expect(TComma);
			expect(TLbl);
			curb->s2 = findblk(tokval.str);
		}
	Close:
		expect(TNL);
		closeblk();
		return PLbl;
	}
	r = varref(tokval.str);
	expect(TEq);
	switch (next()) {
	case TCopy:
		op = OCopy;
		break;
	case TAdd:
		op = OAdd;
		break;
	case TSub:
		op = OSub;
		break;
	case TDiv:
		op = ODiv;
		break;
	case TMod:
		op = OMod;
		break;
	case TPhi:
		if (ps != PPhi)
			err("unexpected phi instruction");
		op = -1;
		break;
	default:
		err("invalid instruction");
	}
	i = 0;
	if (peek() != TNL)
		for (;;) {
			if (i == NPred)
				err("too many arguments");
			if (op == -1) {
				expect(TLbl);
				blk[i] = findblk(tokval.str);
			}
			arg[i] = parseref();
			if (arg[i] == R)
				err("invalid instruction argument");
			i++;
			t = peek();
			if (t == TNL)
				break;
			if (t != TComma)
				err("comma or end of line expected");
			next();
		}
	next();
	if (op != -1 && i != opdesc[op].arity)
		err("invalid arity");
	if (op != -1) {
		if (curi - ins >= NIns)
			err("too many instructions in block");
		curi->op = op;
		curi->to = r;
		curi->l = arg[0];
		curi->r = arg[1];
		curi++;
		return PIns;
	} else {
		phi = alloc(sizeof *phi);
		phi->to = r;
		memcpy(phi->arg, arg, i * sizeof arg[0]);
		memcpy(phi->blk, blk, i * sizeof blk[0]);
		phi->narg = i;
		*plink = phi;
		plink = &phi->link;
		return PPhi;
	}
}

Fn *
parsefn(FILE *f)
{
	int i;
	PState ps;
	Fn *fn;

	inf = f;
	for (i=0; i<NBlk; i++) {
		bmap[i].name[0] = 0;
		bmap[i].blk = 0;
	}
	for (i=Tmp0; i<NSym; i++) {
		sym[i].type = SUndef;
		sym[i].name[0] = 0;
		sym[i].blk = 0;
	}
	ntmp = Tmp0;
	curi = ins;
	curb = 0;
	lnum = 1;
	nblk = 0;
	fn = alloc(sizeof *fn);
	blink = &fn->start;
	ps = PLbl;
	do
		ps = parseline(ps);
	while (ps != PEnd);
	if (curb->jmp.type == JXXX)
		err("last block misses jump");
	fn->sym = alloc(ntmp * sizeof sym[0]);
	memcpy(fn->sym, sym, ntmp * sizeof sym[0]);
	fn->ntmp = ntmp;
	fn->nblk = nblk;
	fn->rpo = 0;
	return fn;
}

static void
printref(Ref r, Fn *fn, FILE *f)
{
	switch (r&RMask) {
	case RSym:
		fprintf(f, "%%%s", fn->sym[r>>RShift].name);
		break;
	case RConst:
		fprintf(f, "%d", r>>RShift);
		break;
	}
}

void
printfn(Fn *fn, FILE *f)
{
	Blk *b;
	Phi *p;
	Ins *i;
	uint n;

	for (b=fn->start; b; b=b->link) {
		fprintf(f, "@%s\n", b->name);
		for (p=b->phi; p; p=p->link) {
			fprintf(f, "\t");
			printref(p->to, fn, f);
			fprintf(f, " = phi ");
			assert(p->narg);
			for (n=0;; n++) {
				fprintf(f, "@%s ", p->blk[n]->name);
				printref(p->arg[n], fn, f);
				if (n == p->narg-1) {
					fprintf(f, "\n");
					break;
				} else
					fprintf(f, ", ");
			}
		}
		for (i=b->ins; i-b->ins < b->nins; i++) {
			fprintf(f, "\t");
			printref(i->to, fn, f);
			assert(opdesc[i->op].name);
			fprintf(f, " = %s", opdesc[i->op].name);
			n = opdesc[i->op].arity;
			if (n > 0) {
				fprintf(f, " ");
				printref(i->l, fn, f);
			}
			if (n > 1) {
				fprintf(f, ", ");
				printref(i->r, fn, f);
			}
			fprintf(f, "\n");
		}
		switch (b->jmp.type) {
		case JRet:
			fprintf(f, "\tret\n");
			break;
		case JJmp:
			if (b->s1 != b->link)
				fprintf(f, "\tjmp @%s\n", b->s1->name);
			break;
		case JJez:
			fprintf(f, "\tjez ");
			printref(b->jmp.arg, fn, f);
			fprintf(f, ", @%s, @%s\n", b->s1->name, b->s2->name);
			break;
		}
	}
}


int
main(int ac, char *av[])
{
	int opt;
	int tx, ntmp;
	Fn *fn;

	fn = parsefn(stdin);

	opt = 0;
	if (ac > 1 && av[1][0] == '-')
		opt = av[1][1];

	switch (opt) {
	case 'f':
		fprintf(stderr, "[test ssafix:");
		fillpreds(fn);
		for (ntmp=fn->ntmp, tx=Tmp0; tx<ntmp; tx++) {
			fprintf(stderr, " %s", fn->sym[tx].name);
			ssafix(fn, tx);
		}
		fprintf(stderr, "]\n");
		break;
	}

	printfn(fn, stdout);
	return 0;
}