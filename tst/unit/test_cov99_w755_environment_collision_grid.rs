//@ crate-type: lib
#![allow(dead_code)]

struct Cell<T, const N: usize>(T);

trait Family {
    type Item;
}

macro_rules! columns {
    ([], $row:expr, $column:expr) => {
        pub fn retain<T>(
            value: <crate::Cell<T, { $row }> as crate::Family>::Item,
        ) -> crate::Cell<T, { $column }>
        where
            crate::Cell<T, { $row }>: crate::Family<Item = crate::Cell<T, { $column }>>,
        {
            value
        }
    };
    ([$next:tt $($rest:tt)*], $row:expr, $column:expr) => {
        mod low {
            columns!([$($rest)*], $row, ($column * 2));
        }
        mod high {
            columns!([$($rest)*], $row, ($column * 2 + 1));
        }
    };
}

macro_rules! rows {
    ([], $row:expr) => {
        columns!([x x x x x x], $row, 0);
    };
    ([$next:tt $($rest:tt)*], $row:expr) => {
        mod low {
            rows!([$($rest)*], ($row * 2));
        }
        mod high {
            rows!([$($rest)*], ($row * 2 + 1));
        }
    };
}

rows!([x x x x x x], 0);
