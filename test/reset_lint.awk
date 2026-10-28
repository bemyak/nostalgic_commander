# Names of the writable s_* state globals declared at file scope: one
# declaration per line start, any type spelling, arrays of any rank, and every
# declarator of a comma list. Immutable ones (const data, `T* const`) are
# skipped; `const T* s_x` is a mutable pointer and is kept.
/^[A-Za-z_]/ && !/^(typedef|extern|return|_Static_assert)[ (]/ {
  head = $0
  cut = match(head, /[=;]/)
  if (cut) head = substr(head, 1, cut - 1)
  if (head ~ /\(/) next
  if (!match(head, /s_[a-z0-9_]+/)) next
  prefix = substr(head, 1, RSTART - 1)
  if (prefix !~ /^[A-Za-z_][A-Za-z0-9_ ]*[ *]+$/) next
  if (prefix ~ /const *$/ || (prefix ~ /const/ && prefix !~ /\*/)) next
  print substr(head, RSTART, RLENGTH)
  rest = substr($0, RSTART + RLENGTH)
  while (match(rest, /, *[*]?s_[a-z0-9_]+ *(\[[^]]*\])* *[=;,]/)) {
    seg = substr(rest, RSTART, RLENGTH)
    match(seg, /s_[a-z0-9_]+/)
    print substr(seg, RSTART, RLENGTH)
    rest = substr(rest, RSTART + RLENGTH - 1)
  }
}
