#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast {

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace {
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
}  // namespace

namespace detail {

class ReturnObj : public std::exception {
public:
    explicit ReturnObj(runtime::ObjectHolder obj)
    : std::exception()
    , obj_(std::move(obj))
    {}

    runtime::ObjectHolder GetObj() const {
        return obj_;
    }

private:
    runtime::ObjectHolder obj_;
};

void ThrowClassIntanceCastError(const ObjectHolder& obj, const std::string& where) {
    using namespace std::literals;
    if (!obj) {
        throw std::runtime_error("Trying to "s + where + " in <None> object"s);
    }
    if (const auto num_ptr = obj.TryAs<runtime::Number>()) {
        throw std::runtime_error("Trying to "s + where + " in <number> object: "s
                + std::to_string(num_ptr->GetValue()));
    }
    if (const auto str_ptr = obj.TryAs<runtime::String>()) {
        throw std::runtime_error("Trying to "s + where + " in <string> object: \""s
                + str_ptr->GetValue() + "\""s);
    }
    if (const auto bool_ptr = obj.TryAs<runtime::Bool>()) {
        throw std::runtime_error("Trying to "s + where + " in <bool> object"s);
    }
    if (const auto cls_ptr = obj.TryAs<runtime::Class>()) {
        throw std::runtime_error("Trying to "s + where + " in <class> object: \""s
                + cls_ptr->GetName() + "\""s);
    }
    throw std::runtime_error("Fail on cast to <runtime::ClassInstance> in "s + where);
}

}  // namespace detail

// ----------- VariableValue -----------------------

VariableValue::VariableValue(const std::string& var_name) {
    dotted_ids_.emplace_back(var_name);
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
    : dotted_ids_(std::move(dotted_ids))
    {}

ObjectHolder VariableValue::Execute(Closure& closure,
                   [[maybe_unused]] Context& context) {
    using namespace std::literals;

    if (!closure.count(dotted_ids_.front())) {
        throw std::runtime_error("No field with name \""s + dotted_ids_.front() + "\""s);
    }

    if (dotted_ids_.size() == 1) {
        return closure.at(dotted_ids_.front());
    }
    auto cls_inst_ptr = closure.at(dotted_ids_.front())
                                      .TryAs<runtime::ClassInstance>();
    if (!cls_inst_ptr) {
        throw std::runtime_error("Failed to cast \""s + dotted_ids_.front() + "\" to <ClassInstance>"s);
    }

    for (size_t i = 1u; i + 1u < dotted_ids_.size(); ++i) {
        if (!cls_inst_ptr->Fields().count(dotted_ids_[i])) {
            throw std::runtime_error("No field with name \""s + dotted_ids_[i] + "\""s);
        }
        cls_inst_ptr = cls_inst_ptr->Fields()
                .at(dotted_ids_[i]).TryAs<runtime::ClassInstance>();
        if (!cls_inst_ptr) {
            throw std::runtime_error("Failed to cast \""s + dotted_ids_[i] + "\" to <ClassInstance>"s);
        }
    }

    if (!cls_inst_ptr->Fields().count(dotted_ids_.back())) {
        throw std::runtime_error("No field with name \""s + dotted_ids_.back() + "\""s);
    }
    return cls_inst_ptr->Fields().at(dotted_ids_.back());
}

// ----------- Assignment -----------------------

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
    : var_name_(std::move(var))
    , value_(std::move(rv))
    {}

ObjectHolder Assignment::Execute(Closure& closure,
                [[maybe_unused]] Context& context) {
    closure[var_name_] = value_->Execute(closure, context);
    return closure.at(var_name_);
}

// ----------- FieldAssignment -----------------------

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                 std::unique_ptr<Statement> rv)
    : object_(std::move(object))
    , field_name_(std::move(field_name))
    , field_value_(std::move(rv))
    {}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context) {
    using namespace std::literals;

    const auto cls_inst_ptr = object_.Execute(closure, context)
                                .TryAs<runtime::ClassInstance>();
    if (!cls_inst_ptr) {
        detail::ThrowClassIntanceCastError(object_.Execute(closure, context), "FieldAssignment"s);
    }
    cls_inst_ptr->Fields()[field_name_] = field_value_->Execute(closure, context);

    return cls_inst_ptr->Fields().at(field_name_);
}

// ----------- Print -----------------------

Print::Print(unique_ptr<Statement> argument) {
    args_.emplace_back(std::move(argument));
}

Print::Print(vector<unique_ptr<Statement>> args)
    : args_(std::move(args))
    {}

unique_ptr<Print> Print::Variable(const std::string& name) {
    return std::make_unique<Print>(std::make_unique<VariableValue>(name));
}

ObjectHolder Print::Execute(Closure& closure, Context& context) {
    std::ostream& out = context.GetOutputStream();
    bool is_not_first = false;
    for (const auto& arg : args_) {
        if (is_not_first) {
            out << " "s;
        } else {
            is_not_first = true;
        }
        const auto& obj = arg->Execute(closure, context);
        if (!obj) {
            out << "None"s;
            continue;
        }
        obj->Print(out, context);
    }
    out << endl;
    return ObjectHolder();
}

// ----------- MethodCall -----------------------

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args)
    : object_(std::move(object))
    , method_name_(std::move(method))
    , args_(std::move(args))
    {}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context) {
    using namespace std::literals;

    const auto cls_inst_ptr = object_->Execute(closure, context)
                                .TryAs<runtime::ClassInstance>();
    if (!cls_inst_ptr) {
        detail::ThrowClassIntanceCastError(object_->Execute(closure, context), "MethodCall"s);
    }

    std::vector<ObjectHolder> actual_args;
    for (const auto& arg : args_) {
        actual_args.emplace_back(arg->Execute(closure, context));
    }

    return cls_inst_ptr->Call(method_name_, actual_args, context);
}

// ----------- NewInstance -----------------------

NewInstance::NewInstance(const runtime::Class& class_)
    : cls_inst_(class_)
    {}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args)
    : cls_inst_(class_)
    , args_(std::move(args))
    {}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context) {
    if (cls_inst_.HasMethod(INIT_METHOD, args_.size())) {
        std::vector<ObjectHolder> actual_args;
        for (const auto& arg : args_) {
            actual_args.emplace_back(arg->Execute(closure, context));
        }
        cls_inst_.Call(INIT_METHOD, actual_args, context);
    }

    return ObjectHolder::Share(cls_inst_);
}

// ----------- UnaryOperation -----------------------

UnaryOperation::UnaryOperation(std::unique_ptr<Statement> argument)
    : arg_(std::move(argument))
    {}

// ----------- Stringify -----------------------

ObjectHolder Stringify::Execute(Closure& closure, Context& context) {
    using namespace std::literals;
    std::ostringstream out;
    const auto& obj = arg_->Execute(closure, context);
    if (!obj) {
        out << "None"s;
    } else {
        obj->Print(out, context);
    }
    return ObjectHolder::Own(runtime::String(out.str()));
}

// ----------- BinaryOperation -----------------------

BinaryOperation::BinaryOperation(std::unique_ptr<Statement> lhs, std::unique_ptr<Statement> rhs)
    : lhs_(std::move(lhs))
    , rhs_(std::move(rhs))
    {}

// ----------- Add -----------------------

ObjectHolder Add::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (const auto lhs_num_ptr = lhs_obj.TryAs<runtime::Number>(),
                   rhs_num_ptr = rhs_obj.TryAs<runtime::Number>();
                   lhs_num_ptr && rhs_num_ptr) {
        const auto sum = lhs_num_ptr->GetValue() + rhs_num_ptr->GetValue();
        return ObjectHolder::Own(runtime::Number(sum));
    }
    if (const auto lhs_str_ptr = lhs_obj.TryAs<runtime::String>(),
                   rhs_str_ptr = rhs_obj.TryAs<runtime::String>();
                   lhs_str_ptr && rhs_str_ptr) {
        const auto sum = lhs_str_ptr->GetValue() + rhs_str_ptr->GetValue();
        return ObjectHolder::Own(runtime::String(sum));
    }
    if (const auto lhs_cls_inst_ptr = lhs_obj.TryAs<runtime::ClassInstance>()) {
        if (lhs_cls_inst_ptr->HasMethod(ADD_METHOD, 1u)) {
            return lhs_cls_inst_ptr->Call(ADD_METHOD, {rhs_obj}, context);
        }
    }
    throw std::runtime_error("Failed on Add operation"s);
}

// ----------- Sub -----------------------

ObjectHolder Sub::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (const auto lhs_num_ptr = lhs_obj.TryAs<runtime::Number>(),
                   rhs_num_ptr = rhs_obj.TryAs<runtime::Number>();
                   lhs_num_ptr && rhs_num_ptr) {
        const auto sum = lhs_num_ptr->GetValue() - rhs_num_ptr->GetValue();
        return ObjectHolder::Own(runtime::Number(sum));
    }
    throw std::runtime_error("Failed on Sub operation"s);
}

// ----------- Mult -----------------------

ObjectHolder Mult::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (const auto lhs_num_ptr = lhs_obj.TryAs<runtime::Number>(),
                   rhs_num_ptr = rhs_obj.TryAs<runtime::Number>();
                   lhs_num_ptr && rhs_num_ptr) {
        const auto sum = lhs_num_ptr->GetValue() * rhs_num_ptr->GetValue();
        return ObjectHolder::Own(runtime::Number(sum));
    }
    throw std::runtime_error("Failed on Mult operation"s);
}

// ----------- Div -----------------------

ObjectHolder Div::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (const auto lhs_num_ptr = lhs_obj.TryAs<runtime::Number>(),
                   rhs_num_ptr = rhs_obj.TryAs<runtime::Number>();
                   lhs_num_ptr && rhs_num_ptr && rhs_num_ptr->GetValue()) {
        const auto sum = lhs_num_ptr->GetValue() / rhs_num_ptr->GetValue();
        return ObjectHolder::Own(runtime::Number(sum));
    }
    throw std::runtime_error("Failed on Div operation"s);
}

// ----------- Or -----------------------

ObjectHolder Or::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (IsTrue(lhs_obj) || IsTrue(rhs_obj)) {
        return ObjectHolder::Own(runtime::Bool(true));
    }
    return ObjectHolder::Own(runtime::Bool(false));
}

// ----------- And -----------------------

ObjectHolder And::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    if (IsTrue(lhs_obj) && IsTrue(rhs_obj)) {
        return ObjectHolder::Own(runtime::Bool(true));
    }
    return ObjectHolder::Own(runtime::Bool(false));
}

// ----------- Not -----------------------

ObjectHolder Not::Execute(Closure& closure, Context& context) {
    const auto& obj = arg_->Execute(closure, context);
    if (!IsTrue(obj)) {
        return ObjectHolder::Own(runtime::Bool(true));
    }
    return ObjectHolder::Own(runtime::Bool(false));
}

// ----------- Compound -----------------------

void Compound::AddStatement(std::unique_ptr<Statement> stmt) {
    args_.emplace_back(std::move(stmt));
}

ObjectHolder Compound::Execute(Closure& closure, Context& context) {
    for (const auto& arg : args_) {
        arg->Execute(closure, context);
    }
    return ObjectHolder::None();
}

// ----------- MethodBody -----------------------

MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
    : body_(std::move(body))
    {}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context) {
    try {
        const auto& obj = body_->Execute(closure, context);
    } catch (detail::ReturnObj& return_obj) {
        return return_obj.GetObj();
    }
    return ObjectHolder::None();
}

// ----------- Return -----------------------

Return::Return(std::unique_ptr<Statement> statement)
    : statement_(std::move(statement))
    {}

ObjectHolder Return::Execute(Closure& closure, Context& context) {
    const auto& obj = statement_->Execute(closure, context);
    if (obj) {
        throw detail::ReturnObj(obj);
    }
    return obj;
}

// ----------- ClassDefinition -----------------------

ClassDefinition::ClassDefinition(ObjectHolder cls)
    : cls_(std::move(cls))
    {
        using namespace std::literals;
        if (!cls_ || !cls_.TryAs<runtime::Class>()) {
            throw logic_error("Wrong class definition"s);
        }
    }

ObjectHolder ClassDefinition::Execute(Closure& closure,
                     [[maybe_unused]] Context& context) {
    closure[cls_.TryAs<runtime::Class>()->GetName()] = cls_;
    return cls_;
}

// ----------- IfElse -----------------------

IfElse::IfElse(std::unique_ptr<Statement> condition,
               std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body)
    : condition_(std::move(condition))
    , if_body_(std::move(if_body))
    , else_body_(std::move(else_body))
    {}

ObjectHolder IfElse::Execute(Closure& closure, Context& context) {
    if (IsTrue(condition_->Execute(closure, context))) {
        return if_body_->Execute(closure, context);
    }
    if (else_body_) {
        return else_body_->Execute(closure, context);
    }
    return {};
}

// ----------- Comparison -----------------------

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs))
    , cmp_(cmp)
    {}

ObjectHolder Comparison::Execute(Closure& closure, Context& context) {
    const auto& lhs_obj = lhs_->Execute(closure, context);
    const auto& rhs_obj = rhs_->Execute(closure, context);
    return ObjectHolder::Own(runtime::Bool(cmp_(lhs_obj, rhs_obj, context)));
}



}  // namespace ast
