#![feature(cfg_select)]

fn pick() -> u32 {
    cfg_select! {
        windows => 1,
        unix => { 2 }
        _ => 3,
    }
}

fn last() -> &'static str {
    cfg_select! {
        windows => "windows",
        _ => if pick() == 2 { "two" } else { "other" }
    }
}

fn main() {
    assert_eq!(pick(), 2);
    assert_eq!(last(), "two");
}
