#![feature(try_blocks)]

fn leave_loop() -> u16 {
    loop {
        let _: Result<(), u8> = try {
            Ok::<(), u8>(())?;
            break 947u16;
        };
    }
}

fn sum_loop(fail: bool) -> Result<u16, u8> {
    let mut sum = 0u16;
    let mut iteration = 0u16;
    let mut entered = 0u16;
    while iteration < 5 {
        iteration += 1;
        let piece: Result<u16, u8> = try {
            entered += 1;
            assert!(entered <= 5);
            if iteration % 2 == 0 {
                continue;
            }
            if fail && iteration == 3 {
                Err::<(), u8>(17)?;
            }
            iteration
        };
        sum += piece?;
    }
    Ok(sum)
}

fn main() {
    assert_eq!(leave_loop(), 947);
    assert_eq!(sum_loop(false), Ok(9));
    assert_eq!(sum_loop(true), Err(17));
}
