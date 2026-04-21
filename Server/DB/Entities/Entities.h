#pragma once

#include "../ORM/Types.h"
#include "../ORM/Attributes.h"
#include "../ORM/Property.h"
#include "../ORM/PrimaryProperty.h"
#include <string>

DB_ENTITY
struct User
{
    PrimaryProperty<int64>   Id;
    Property<std::string>    Email;
    Property<std::string>    Nickname;
    Property<std::string>    Password;
};
