/**
 * @brief Held pose parts: which part of each pose group shows, chosen over IPC.
 *
 * A pose3.json lists groups of parts of which one shows at a time (Hiyori:
 * PartArmA / PartArmB). Motions pick the part through PartOpacity curves,
 * which the Cubism motion writes as the parameter named like the part.
 * A held part is written over idle motions every frame; a motion playing
 * above idle priority still drives the parts while it plays, and the held
 * part comes back after. Releasing a group puts its first part back once
 * (the pose's default) and leaves the group to the motions again.
 *
 * Kept free of the Cubism SDK so it can be tested on its own; LAppModel
 * turns the returned part values into parameter writes. C++14, STL only.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class PoseHold
{
public:
    /// Each pose group as its list of part ids, in pose3.json order.
    std::vector<std::vector<std::string> > groups;

    /// Holds `part` in `group`. False when either is not in the pose.
    bool Set(int group, const std::string& part)
    {
        if (group < 0 || group >= (int)groups.size()) return false;
        const std::vector<std::string>& parts = groups[group];
        for (size_t i = 0; i < parts.size(); i++)
        {
            if (parts[i] == part)
            {
                _held[group] = (int)i;
                _released.erase(group);
                return true;
            }
        }
        return false;
    }

    /// Gives `group` back to the model. False when there is no such group.
    bool Release(int group)
    {
        if (group < 0 || group >= (int)groups.size()) return false;
        if (_held.erase(group) > 0) _released.insert(group);
        return true;
    }

    /// The part held in `group`, or "" when none is.
    std::string Held(int group) const
    {
        std::map<int, int>::const_iterator it = _held.find(group);
        return it == _held.end() ? std::string() : groups[group][it->second];
    }

    /**
     * @brief The part values (1 shown, 0 hidden) to write this frame.
     * @param motionPlaying  True while a motion above idle priority plays:
     *                       it drives the parts, so nothing is written.
     */
    std::vector<std::pair<std::string, float> > Values(bool motionPlaying)
    {
        std::vector<std::pair<std::string, float> > out;
        if (motionPlaying) return out;
        for (std::map<int, int>::const_iterator it = _held.begin(); it != _held.end(); ++it)
        {
            Show(it->first, it->second, out);
        }
        for (std::set<int>::const_iterator it = _released.begin(); it != _released.end(); ++it)
        {
            Show(*it, 0, out);
        }
        _released.clear();
        return out;
    }

private:
    std::map<int, int> _held;  ///< group -> held part index
    std::set<int> _released;   ///< groups whose default part is still to be put back

    void Show(int group, int shown, std::vector<std::pair<std::string, float> >& out) const
    {
        const std::vector<std::string>& parts = groups[group];
        for (size_t i = 0; i < parts.size(); i++)
        {
            out.push_back(std::make_pair(parts[i], (int)i == shown ? 1.0f : 0.0f));
        }
    }
};
