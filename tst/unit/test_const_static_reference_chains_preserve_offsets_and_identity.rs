static WORDS: [u16; 4] = [11, 19, 31, 43];
static LINKS: [&[u16; 4]; 2] = [&WORDS, &WORDS];

const fn last(links: &'static [&'static [u16; 4]; 2]) -> &'static u16 {
    let words = links[1];
    &words[3]
}

const LAST: &u16 = last(&LINKS);
const SUM: u16 = LINKS[0][1] + *LAST;

fn main() {
    assert_eq!(*LAST, 43);
    assert_eq!(SUM, 62);
    assert!(core::ptr::eq(LAST, &WORDS[3]));
    assert_eq!(match &WORDS[3] { LAST => 7, _ => 0 }, 7);
    assert_eq!(match &WORDS[2] { LAST => 7, _ => 0 }, 0);
}
