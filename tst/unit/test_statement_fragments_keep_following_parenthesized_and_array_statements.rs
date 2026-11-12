macro_rules! statements {
    ($($statement:stmt)*) => {
        $($statement)*
    };
}

fn main() {
    let mut total = 0u16;

    statements! {
        if true {
            total += 1;
        }
        ({ total += 2; total },);
        loop {
            total += 4;
            break;
        }
        [{ total += 8; total }];
    }

    assert_eq!(total, 15);
}
