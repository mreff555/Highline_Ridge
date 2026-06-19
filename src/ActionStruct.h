#ifndef ACTION_STRUCT_H
#define ACTION_STRUCT_H

namespace testgame
{
    struct ActionStruct
    {
        bool examine = false;
        bool speak = false;
        bool hit = false;
        bool use = false;
        bool take = false;
    };
}

#endif /* ACTION_STRUCT_H */