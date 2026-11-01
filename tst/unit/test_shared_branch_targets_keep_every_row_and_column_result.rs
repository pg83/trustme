macro_rules! columns {
    ($column:expr, $flag:expr, $row:literal; $($value:literal),*) => {
        match $column {
            $($value => if $flag { return 100 + $row; } else { return 100 + $value; },)*
            _ => return 999,
        }
    };
}

macro_rules! rows {
    ($row:expr, $column:expr, $flag:expr; $($value:literal),*) => {
        match $row {
            $($value => columns!($column, $flag, $value; 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),)*
            _ => return 777,
        }
    };
}

#[inline(never)]
fn select(row: u8, column: u8, flag: bool) -> u16 {
    rows!(row, column, flag; 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31)
}

fn main() {
    for row in 0u8..32 {
        for column in 0u8..32 {
            assert_eq!(select(row, column, true), 100 + row as u16);
            assert_eq!(select(row, column, false), 100 + column as u16);
        }
    }
    assert_eq!(select(99, 0, true), 777);
    assert_eq!(select(0, 99, false), 999);
}
