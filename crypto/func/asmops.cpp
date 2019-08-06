#include "parser/srcread.h"
#include "func.h"
#include <iostream>

namespace funC {

/*
 * 
 *   ASM-OP LIST FUNCTIONS
 * 
 */

int is_pos_pow2(td::RefInt256 x) {
  if (sgn(x) > 0 && !sgn(x & (x - 1))) {
    return x->bit_size(false) - 1;
  } else {
    return -1;
  }
}

int is_neg_pow2(td::RefInt256 x) {
  return sgn(x) < 0 ? is_pos_pow2(-x) : 0;
}

AsmOp AsmOp::Const(int arg, std::string push_op) {
  std::ostringstream os;
  os << arg << ' ' << push_op;
  return AsmOp::Custom(os.str());
}

AsmOp AsmOp::make_stk2(int a, int b, const char* str, int delta) {
  std::ostringstream os;
  os << "s" << a << " s" << b << " " << str;
  int c = std::max(a, b) + 1;
  return AsmOp::Custom(os.str(), c, c + delta);
}

AsmOp AsmOp::make_stk3(int a, int b, int c, const char* str, int delta) {
  std::ostringstream os;
  os << "s" << a << " s" << b << " s" << c << " " << str;
  int m = std::max(a, std::max(b, c)) + 1;
  return AsmOp::Custom(os.str(), m, m + delta);
}

AsmOp AsmOp::BlkSwap(int a, int b) {
  std::ostringstream os;
  if (a == 1 && b == 1) {
    return AsmOp::Xchg(0, 1);
  } else if (a == 1) {
    if (b == 2) {
      os << "ROT";
    } else {
      os << b << " ROLL";
    }
  } else if (b == 1) {
    if (a == 2) {
      os << "-ROT";
    } else {
      os << a << " -ROLL";
    }
  } else {
    os << a << " " << b << " BLKSWAP";
  }
  return AsmOp::Custom(os.str(), a + b, a + b);
}

AsmOp AsmOp::BlkPush(int a, int b) {
  std::ostringstream os;
  if (a == 1) {
    return AsmOp::Push(b);
  } else if (a == 2 && b == 1) {
    os << "2DUP";
  } else {
    os << a << " " << b << " BLKPUSH";
  }
  return AsmOp::Custom(os.str(), b + 1, a + b + 1);
}

AsmOp AsmOp::BlkDrop(int a) {
  std::ostringstream os;
  if (a == 1) {
    return AsmOp::Pop();
  } else if (a == 2) {
    os << "2DROP";
  } else {
    os << a << " BLKDROP";
  }
  return AsmOp::Custom(os.str(), a, 0);
}

AsmOp AsmOp::BlkReverse(int a, int b) {
  std::ostringstream os;
  os << a << " " << b << " REVERSE";
  return AsmOp::Custom(os.str(), a + b, a + b);
}

AsmOp AsmOp::IntConst(td::RefInt256 x) {
  if (x->signed_fits_bits(8)) {
    return AsmOp::Const(dec_string(std::move(x)) + " PUSHINT");
  }
  if (!x->is_valid()) {
    return AsmOp::Const("PUSHNAN");
  }
  int k = is_pos_pow2(x);
  if (k >= 0) {
    return AsmOp::Const(k, "PUSHPOW2");
  }
  k = is_pos_pow2(x + 1);
  if (k >= 0) {
    return AsmOp::Const(k, "PUSHPOW2DEC");
  }
  k = is_pos_pow2(-x);
  if (k >= 0) {
    return AsmOp::Const(k, "PUSHNEGPOW2");
  }
  return AsmOp::Const(dec_string(std::move(x)) + " PUSHINT");
}

void AsmOp::out(std::ostream& os) const {
  if (!op.empty()) {
    os << op;
    return;
  }
  switch (t) {
    case a_none:
      break;
    case a_xchg:
      if (!a && !(b & -2)) {
        os << (b ? "SWAP" : "NOP");
        break;
      }
      os << "s" << a << " s" << b << " XCHG";
      break;
    case a_push:
      if (!(a & -2)) {
        os << (a ? "OVER" : "DUP");
        break;
      }
      os << "s" << a << " PUSH";
      break;
    case a_pop:
      if (!(a & -2)) {
        os << (a ? "NIP" : "DROP");
        break;
      }
      os << "s" << a << " POP";
      break;
    default:
      throw src::Fatal{"unknown assembler operation"};
  }
}

void AsmOp::out_indent_nl(std::ostream& os, bool no_eol) const {
  for (int i = 0; i < indent; i++) {
    os << "  ";
  }
  out(os);
  if (!no_eol) {
    os << std::endl;
  }
}

std::string AsmOp::to_string() const {
  if (!op.empty()) {
    return op;
  } else {
    std::ostringstream os;
    out(os);
    return os.str();
  }
}

const_idx_t AsmOpList::register_const(Const new_const) {
  if (new_const.is_null()) {
    return not_const;
  }
  unsigned idx;
  for (idx = 0; idx < constants_.size(); idx++) {
    if (!td::cmp(new_const, constants_[idx])) {
      return idx;
    }
  }
  constants_.push_back(std::move(new_const));
  return (const_idx_t)idx;
}

Const AsmOpList::get_const(const_idx_t idx) {
  if ((unsigned)idx < constants_.size()) {
    return constants_[idx];
  } else {
    return {};
  }
}

void AsmOpList::show_var(std::ostream& os, var_idx_t idx) const {
  if (!var_names_ || (unsigned)idx >= var_names_->size()) {
    os << '_' << idx;
  } else {
    var_names_->at(idx).show(os, 2);
  }
}

void AsmOpList::show_var_ext(std::ostream& os, std::pair<var_idx_t, const_idx_t> idx_pair) const {
  auto i = idx_pair.first;
  auto j = idx_pair.second;
  if (!var_names_ || (unsigned)i >= var_names_->size()) {
    os << '_' << i;
  } else {
    var_names_->at(i).show(os, 2);
  }
  if ((unsigned)j < constants_.size() && constants_[j].not_null()) {
    os << '=' << constants_[j];
  }
}

void AsmOpList::out(std::ostream& os, int mode) const {
  if (!(mode & 2)) {
    for (const auto& op : list_) {
      op.out_indent_nl(os);
    }
  } else {
    std::size_t n = list_.size();
    for (std::size_t i = 0; i < n; i++) {
      const auto& op = list_[i];
      if (!op.is_comment() && i + 1 < n && list_[i + 1].is_comment()) {
        op.out_indent_nl(os, true);
        os << '\t';
        do {
          i++;
        } while (i + 1 < n && list_[i + 1].is_comment());
        list_[i].out(os);
        os << std::endl;
      } else {
        op.out_indent_nl(os, false);
      }
    }
  }
}

bool apply_op(StackTransform& trans, const AsmOp& op) {
  if (!trans.is_valid()) {
    return false;
  }
  switch (op.t) {
    case AsmOp::a_none:
      return true;
    case AsmOp::a_xchg:
      return trans.apply_xchg(op.a, op.b, true);
    case AsmOp::a_push:
      return trans.apply_push(op.a);
    case AsmOp::a_pop:
      return trans.apply_pop(op.a);
    case AsmOp::a_const:
      return !op.a && op.b == 1 && trans.apply_push_newconst();
    default:
      return false;
  }
}

}  // namespace funC