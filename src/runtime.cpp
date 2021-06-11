#include "runtime.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime {

// ------------ ObjectHolder --------------------
ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data)) {
}

void ObjectHolder::AssertIsValid() const {
    assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object) {
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/) { /* do nothing */ }));
}

ObjectHolder ObjectHolder::None() {
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const {
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const {
    AssertIsValid();
    return Get();
}

Object* ObjectHolder::Get() const {
    return data_.get();
}

ObjectHolder::operator bool() const {
    return Get() != nullptr;
}

// ------------ Class --------------------

Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
    : name_(std::move(name))
    , methods_(std::move(methods))
    , parent_(parent)
    {}

const Method* Class::GetMethod(const std::string& name) const {
    auto result = std::find_if(methods_.begin(), methods_.end(),
            [&name](const auto& method){return name == method.name;});
    if (result == methods_.end()) {
        if (parent_) {
            return parent_->GetMethod(name);
        } else {
            return nullptr;
        }
    }
    return &(*result);
}

[[nodiscard]] const std::string& Class::GetName() const {
    return name_;
}

void Class::Print(ostream& os, [[maybe_unused]] Context& context) {
    using namespace std::literals;
    os << "Class "s << GetName();
}

// ------------ Bool --------------------

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context) {
    os << (GetValue() ? "True"sv : "False"sv);
}

// ------------ ClassInstance --------------------

ClassInstance::ClassInstance(const Class& cls)
    : cls_(cls)
    {
        using namespace std::literals;
        fields_["self"] = ObjectHolder::Share(*this);
    }

void ClassInstance::Print(std::ostream& os, Context& context) {
    using namespace std::literals;
    if (HasMethod("__str__"s, 0u)) {
        const auto& obj = Call("__str__"s, {}, context);
        if (const auto num_ptr = obj.TryAs<Number>()) {
            os << num_ptr->GetValue();
        } else if (const auto str_ptr = obj.TryAs<String>()) {
            os << str_ptr->GetValue();
        } else if (const auto cls_ptr = obj.TryAs<Class>()) {
            cls_ptr->Print(os, context);
        } else if (const auto cls_inst_ptr = obj.TryAs<ClassInstance>()) {
            cls_inst_ptr->Print(os, context);
        }
    } else {
        os << this;
    }

}

bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const {
    const auto method_ptr = cls_.GetMethod(method);
    if (!method_ptr) {
        return false;
    }
    if (method_ptr->formal_params.size() != argument_count) {
        return false;
    }
    return true;
}

Closure& ClassInstance::Fields() {
    return fields_;
}

const Closure& ClassInstance::Fields() const {
    return fields_;
}

ObjectHolder ClassInstance::Call(const std::string& method,
                                 const std::vector<ObjectHolder>& actual_args,
                                 Context& context) {
    if (!HasMethod(method, actual_args.size())) {
        throw std::runtime_error("No such method \""s + method + "\" or wrong count of arguments"s);
    }
    const auto method_ptr = cls_.GetMethod(method);
    Closure arg_name_to_obj;
    arg_name_to_obj["self"s] = ObjectHolder::Share(*this);
    for (size_t i = 0; i < actual_args.size(); ++i) {
        const std::string& name = method_ptr->formal_params[i];
        arg_name_to_obj[name] = actual_args[i];
    }
    return method_ptr->body->Execute(arg_name_to_obj, context);
}



// ------------ other funcs --------------------

bool IsTrue(const ObjectHolder& object) {
    using namespace std::literals;
    if (!object) {
        return false;
    }
    if (const auto num_ptr = object.TryAs<Number>()) {
        return num_ptr->GetValue();
    } else if (const auto str_ptr = object.TryAs<String>()) {
        return !str_ptr->GetValue().empty();
    } else if (const auto bool_ptr = object.TryAs<Bool>()) {
        return bool_ptr->GetValue();
    }
    return false;
}

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    using namespace std::literals;

    if (!lhs && !rhs) {
        return true;
    }
    if (!lhs || !rhs) {
        throw std::runtime_error("Cannot compare objects for equality"s);
    }

    if (const auto lhs_num_ptr = lhs.TryAs<Number>(),
                   rhs_num_ptr = rhs.TryAs<Number>();
                   lhs_num_ptr && rhs_num_ptr) {
        return lhs_num_ptr->GetValue() == rhs_num_ptr->GetValue();
    }
    if (const auto lhs_str_ptr = lhs.TryAs<String>(),
                   rhs_str_ptr = rhs.TryAs<String>();
                   lhs_str_ptr && rhs_str_ptr) {
        return lhs_str_ptr->GetValue() == rhs_str_ptr->GetValue();
    }
    if (const auto lhs_bool_ptr = lhs.TryAs<Bool>(),
                   rhs_bool_ptr = rhs.TryAs<Bool>();
                   lhs_bool_ptr && rhs_bool_ptr) {
        return lhs_bool_ptr->GetValue() == rhs_bool_ptr->GetValue();
    }

    if (const auto lhs_cls_inst_ptr = lhs.TryAs<ClassInstance>()) {
        if (lhs_cls_inst_ptr->HasMethod("__eq__"s, 1u)) {
            return lhs_cls_inst_ptr->Call("__eq__"s, {rhs}, context).TryAs<Bool>()->GetValue();
        }
    }

    throw std::runtime_error("Cannot compare objects for equality"s);
}

bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    using namespace std::literals;

    if (!lhs || !rhs) {
        throw std::runtime_error("Cannot compare objects for less"s);
    }

    if (const auto lhs_num_ptr = lhs.TryAs<Number>(),
                   rhs_num_ptr = rhs.TryAs<Number>();
                   lhs_num_ptr && rhs_num_ptr) {
        return lhs_num_ptr->GetValue() < rhs_num_ptr->GetValue();
    }
    if (const auto lhs_str_ptr = lhs.TryAs<String>(),
                   rhs_str_ptr = rhs.TryAs<String>();
                   lhs_str_ptr && rhs_str_ptr) {
        return lhs_str_ptr->GetValue() < rhs_str_ptr->GetValue();
    }
    if (const auto lhs_bool_ptr = lhs.TryAs<Bool>(),
                   rhs_bool_ptr = rhs.TryAs<Bool>();
                   lhs_bool_ptr && rhs_bool_ptr) {
        return lhs_bool_ptr->GetValue() < rhs_bool_ptr->GetValue();
    }

    if (const auto lhs_cls_inst_ptr = lhs.TryAs<ClassInstance>()) {
        if (lhs_cls_inst_ptr->HasMethod("__lt__"s, 1u)) {
            return lhs_cls_inst_ptr->Call("__lt__"s, {rhs}, context).TryAs<Bool>()->GetValue();
        }
    }

    throw std::runtime_error("Cannot compare objects for less"s);
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context) && !Equal(lhs,rhs, context);
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Greater(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context) {
    return !Less(lhs, rhs, context);
}

}  // namespace runtime
