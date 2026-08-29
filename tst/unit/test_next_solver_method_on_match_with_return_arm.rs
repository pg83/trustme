//@ check-pass

enum Param {
    Number(i32),
}

enum Operation {
    Decimal,
    Octal,
    LowerHex,
    UpperHex,
    String,
}

struct Flags {
    precision: usize,
    alternate: bool,
    sign: bool,
    space: bool,
}

fn render(value: Param, operation: Operation, flags: Flags) -> Result<Vec<u8>, String> {
    let bytes = match value {
        Param::Number(number) => match operation {
            Operation::Decimal => {
                if flags.sign {
                    format!("{:+01$}", number, flags.precision)
                } else if number < 0 {
                    format!("{:01$}", number, flags.precision + 1)
                } else if flags.space {
                    format!(" {:01$}", number, flags.precision)
                } else {
                    format!("{:01$}", number, flags.precision)
                }
            }
            Operation::Octal => {
                if flags.alternate {
                    format!("0{:01$o}", number, flags.precision.saturating_sub(1))
                } else {
                    format!("{:01$o}", number, flags.precision)
                }
            }
            Operation::LowerHex => {
                if flags.alternate && number != 0 {
                    format!("0x{:01$x}", number, flags.precision)
                } else {
                    format!("{:01$x}", number, flags.precision)
                }
            }
            Operation::UpperHex => {
                if flags.alternate && number != 0 {
                    format!("0X{:01$X}", number, flags.precision)
                } else {
                    format!("{:01$X}", number, flags.precision)
                }
            }
            Operation::String => return Err("number used as string".to_string()),
        }
        .into_bytes(),
    };
    Ok(bytes)
}

fn main() {
    let flags = Flags { precision: 0, alternate: false, sign: false, space: false };
    assert_eq!(render(Param::Number(12), Operation::Decimal, flags).unwrap(), b"12");
}
