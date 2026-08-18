// A run of `*`s ends a block comment on whichever of them the `/` follows, and
// a raw lifetime needs raw identifiers, which arrived after Rust 2015.
//@ edition: 2015

macro_rules! ed2015 {
    ('r # lt) => { 1 };
    ($lt:lifetime) => { 2 };
}

/***********
 * A comment whose stars are even in number.
 **********/
/**********
 * And odd.
 *********/
/***/
/****/
/*****/
/* a /* nested */ comment */

fn main() {
    assert_eq!(ed2015!('r#lt), 1);
    let _x = 1;
}
