// sqlparser's parser reassigns tokens in loops; since 4da019c22 a loop head
// gave such a variable a drop flag for every field of every variant, down to
// the leaves, and every `continue` and reassignment wrote or dropped them
// all - sqlparser took over thirty minutes in MIR optimisation instead of
// twenty seconds. rustc's drop elaboration keeps flags only for the move
// paths the body creates: what it moves or assigns. A loop head is now shaped
// along the parts the body moves, assigns or destructures by value.
#[derive(Clone, Debug, Default, PartialEq)]
struct Leaf {
    a: String,
    b: String,
    c: String,
    d: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
struct Node {
    a: Leaf,
    b: Leaf,
    c: Leaf,
    d: Leaf,
}

macro_rules! token {
    ($($variant:ident)*) => {
        #[derive(Clone, Debug, PartialEq)]
        enum Token {
            $($variant(Node, Node),)*
            End,
        }
    };
}

token!(V0 V1 V2 V3 V4 V5 V6 V7 V8 V9 V10 V11 V12 V13 V14 V15 V16 V17 V18 V19 V20 V21 V22 V23 V24 V25 V26 V27 V28 V29 V30 V31 V32 V33 V34 V35 V36 V37 V38 V39 V40 V41 V42 V43 V44 V45 V46 V47 V48 V49 V50 V51 V52 V53 V54 V55 V56 V57 V58 V59 V60 V61 V62 V63 V64 V65 V66 V67 V68 V69 V70 V71 V72 V73 V74 V75 V76 V77 V78 V79 V80 V81 V82 V83 V84 V85 V86 V87 V88 V89 V90 V91 V92 V93 V94 V95 V96 V97 V98 V99 V100 V101 V102 V103 V104 V105 V106 V107 V108 V109 V110 V111 V112 V113 V114 V115 V116 V117 V118 V119 V120 V121 V122 V123 V124 V125 V126 V127 V128 V129 V130 V131 V132 V133 V134 V135 V136 V137 V138 V139 V140 V141 V142 V143 V144 V145 V146 V147 V148 V149 V150 V151 V152 V153 V154 V155 V156 V157 V158 V159);

macro_rules! continue_at {
    ($tok:ident, $i:ident; $($at:literal)*) => {
        $(
            if $i == $at {
                $tok = next($i);
                $i += 1;
                continue;
            }
        )*
    };
}

fn next(i: usize) -> Token {
    if i >= 3 {
        return Token::End;
    }
    let mut node = Node::default();
    node.a.a = format!("{i}");
    Token::V0(node, Node::default())
}

fn walk(n: usize) -> Vec<String> {
    let mut seen = Vec::new();
    let mut tok = next(0);
    let mut i = 0;
    loop {
        match tok {
            Token::V0(node, _) => seen.push(node.a.a),
            Token::End => break,
            other => seen.push(format!("{other:?}")),
        }
        continue_at!(tok, i; 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143 144 145 146 147 148 149 150 151 152 153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 171 172 173 174 175 176 177 178 179);
        i += 1;
        if i > n {
            break;
        }
        tok = next(i);
    }
    seen
}

fn main() {
    assert_eq!(walk(10), ["0", "1", "2"]);
}
