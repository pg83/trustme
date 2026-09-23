// icu_locale_core forwards `$key:literal` into a helper as `$key:expr` and
// the helper hands it on to `subtag!($subtag:literal)`. Upstream records at
// capture whether an `expr` fragment is a literal or a negated literal
// (`can_begin_literal_maybe_minus` of its `MetaVarKind::Expr`), and a
// `literal` matcher then takes such a fragment whole.
macro_rules! text {
    ($s:literal) => {
        $s
    };
}

macro_rules! number {
    ($n:literal) => {
        $n
    };
}

macro_rules! forward {
    ($e:expr) => {
        text!($e)
    };
}

macro_rules! forward_number {
    ($e:expr) => {
        number!($e)
    };
}

macro_rules! pick {
    ($l:literal) => {
        "literal"
    };
    ($e:expr) => {
        "expression"
    };
}

macro_rules! forward_pick {
    ($e:expr) => {
        pick!($e)
    };
}

fn main() {
    assert_eq!(forward!("islamic"), "islamic");
    assert_eq!(forward_number!(-5), -5);
    assert_eq!(forward_number!(2.5), 2.5);
    assert_eq!(forward_pick!(true), "literal");
    assert_eq!(forward_pick!(1 + 2), "expression");
}
