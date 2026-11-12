macro_rules! through_ancestor {
    ($ancestor:path; $statement:stmt) => {{
        use $ancestor as root;
        $statement;
        root::base()
    }};
}

fn base() -> u16 { 431 }
fn bump(value: &mut u16) { *value += 17; }

mod outer {
    pub mod inner {
        pub fn run() -> u16 {
            let mut counter = 13;
            let base = through_ancestor!(super::super; super::super::bump(&mut counter));
            counter + base
        }
    }
}

fn main() {
    assert_eq!(outer::inner::run(), 461);
}
