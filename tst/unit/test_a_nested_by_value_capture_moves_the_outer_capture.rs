//@ edition: 2021
// png's `Reader::next_interlaced_row` returns
// `result.map(move |option| option.map(move |interlace| .. self ..))` with
// `self: &mut Self`. The inner move closure captures `self` by value; for the
// outer closure that is a move of its own capture (rustc: a by-value upvar is
// a consume, and a capture is not a reborrow site), so the outer closure is
// FnOnce. We recorded the move of the `&mut` as a mutable reborrow, called the
// outer closure by reference and moved out of the borrowed environment.
struct Row<'a> {
    data: &'a [u8],
    info: usize,
}

struct Reader {
    scratch: Vec<u8>,
    width: usize,
}

impl Reader {
    fn line_size(&self, info: &usize) -> usize {
        self.width.min(*info)
    }

    fn read_row(&mut self, out: &mut [u8]) -> Result<Option<usize>, ()> {
        out[0] = 1;
        Ok(Some(2))
    }

    fn next_row(&mut self) -> Result<Option<Row<'_>>, ()> {
        let mut buf = std::mem::take(&mut self.scratch);
        buf.resize(self.width, 0u8);
        let result = self.read_row(&mut buf);
        self.scratch = buf;
        result.map(move |option| {
            option.map(move |info| {
                let n = self.line_size(&info);
                Row { data: &self.scratch[..n], info }
            })
        })
    }
}

fn main() {
    let mut r = Reader { scratch: Vec::new(), width: 4 };
    let row = r.next_row().unwrap().unwrap();
    assert_eq!(row.data, &[1, 0]);
    assert_eq!(row.info, 2);

}
