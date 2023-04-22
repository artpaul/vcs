#pragma once

namespace Vcs {

class Datastore;
class HashId;
class Object;

bool CheckConsistency(const HashId& id, const Datastore* odb);

bool CheckConsistency(const Object& obj, const Datastore* odb);

} // namespace Vcs
