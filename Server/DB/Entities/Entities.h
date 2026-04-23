#pragma once

#include "../ORM/Types.h"
#include "../ORM/Attributes.h"
#include "../ORM/Property.h"
#include "../ORM/PrimaryProperty.h"
#include "../ORM/Navigation.h"
#include <string>

namespace FriendStatus
{
    inline constexpr auto Pending  = "Pending";
    inline constexpr auto Accepted = "Accepted";
}

DB_ENTITY
struct User
{
    PrimaryProperty<int64>   Id;
    Property<std::string>    Email;
    Property<std::string>    Nickname;
    Property<std::string>    Password;
};


DB_ENTITY
struct Friendship
{
    PrimaryProperty<int64>      Id;
    Property<int64>             FromUserId;
    Property<int64>             ToUserId;
    Property<std::string>       Status;
    
    FK(FromUserId) Navigation<User>            FromUser;
    FK(ToUserId)   Navigation<User>            ToUser;
};