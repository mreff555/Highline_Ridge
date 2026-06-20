#ifndef MILESTONE_DATABASE_H
#define MILESTONE_DATABASE_H

#include <MilestoneStruct.h>
#include <map>
#include <string>

namespace testgame
{

class MilestoneDatabase
{
public:
    bool load(const std::string& configPath);

    const std::map<std::string, MilestoneDef>& getMilestones() const { return milestones; }
    const MilestoneDef* find(const std::string& milestoneId) const;

private:
    std::map<std::string, MilestoneDef> milestones;
};

}

#endif /* MILESTONE_DATABASE_H */