struct LocalSplitMut<'a, T, P> {
    v: &'a mut [T],
    pred: P,
    finished: bool,
}

impl<'a, T, P> LocalSplitMut<'a, T, P>
where
    P: FnMut(&T) -> bool,
{
    fn next_back(&mut self) -> Option<&'a mut [T]> {
        if self.finished {
            return None;
        }
        let idx_opt = {
            let pred = &mut self.pred;
            self.v.iter().rposition(|x| (*pred)(x))
        };
        match idx_opt {
            None => None,
            Some(idx) => {
                let tmp = core::mem::take(&mut self.v);
                let (head, tail) = tmp.split_at_mut(idx);
                self.v = head;
                Some(&mut tail[1..])
            }
        }
    }
}

fn main() {}

struct ChunkByMut<'a, T: 'a> {
    slice: &'a mut [T],
}

impl<'a, T: 'a> ChunkByMut<'a, T> {
    fn next(&mut self) -> Option<&'a mut [T]> {
        if self.slice.is_empty() {
            None
        } else {
            let mut len = 1;
            let mut iter = self.slice.windows(2);
            while let Some([_, _]) = iter.next() {
                len += 1;
                break;
            }
            let slice = core::mem::take(&mut self.slice);
            let (head, tail) = slice.split_at_mut(len);
            self.slice = tail;
            Some(head)
        }
    }
}
