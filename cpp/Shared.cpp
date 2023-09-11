#include "Shared.hpp"

void SharedVariables::insert(Shared<uint32_t> &variable) {
  Shared<uint32_t>** to = &root_;
  while (*to != nullptr) {
    if (variable.kind_id_ == (*to)->kind_id_) {
      variable.next_ = (*to)->next_;
      (*to)->next_ = &variable;
      return;
    }
    else {
      to = &((*to)->next_id_);
    }
  }
  *to = &variable;
}

void SharedVariables::update(const Packet &packet, uint8_t node_name) {
  Shared<uint32_t>* variable = root_;
  while (variable != nullptr) {
    if (packet.kind_id() == variable->kind_id_) {
      while (variable != nullptr) {

        if (variable->packet_from_ != FROM_ALL
            && packet.from() != variable->packet_from_);
        else if (variable->node_name_ != FROM_ALL
             && node_name != variable->node_name_);
        else {
          Entry entry = packet.find(variable->entry_type_, variable->index_);

          if (entry != packet.end()) {
            variable->value_ = entry.as<uint32_t>();
            variable->last_updated_millis_ = getMillis();
          }
        }

        variable = variable->next_;
      }
      break;
    }
    else variable = variable->next_id_;
  }
}
