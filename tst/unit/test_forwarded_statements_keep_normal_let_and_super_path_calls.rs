fn record(target: &core::cell::Cell<u16>) {
    target.set(1193);
}

mod nested {
    macro_rules! pass_statement {
        ($statement:stmt) => {
            $statement;
        };
    }

    pub fn run() -> u16 {
        let counter = core::cell::Cell::new(0u16);
        pass_statement!(let extra = 7u16);
        pass_statement!(super::record(&counter));
        extra + counter.get()
    }
}

fn main() {
    assert_eq!(nested::run(), 1200);
}
