#include "Shared.hpp"

void SharedVariables::insert(Shared<uint32_t> &variable) {
  Shared<uint32_t>** to = &root_;
  while (*to != nullptr) {
    if (variable.packet_id_ == (*to)->packet_id_) {
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

void SharedVariables::update(const Packet &packet) {
//  printf("%d\n", packet.id());
  Shared<uint32_t>* variable = root_;
  while (variable != nullptr) {
    if (packet.id() == variable->packet_id_) {
      while (variable != nullptr) {
        for (Entry entry = packet.begin(); entry != packet.end(); ++entry) {
//          if (packet.id() == 'P')  {
//            printf("%x\n",variable);
//            printf(".\n");
//          }
          if (entry.type() == variable->entry_type_ &&
              (variable->packet_from_ == 0xFF ||
               packet.from() == variable->packet_from_)) {
//            if (packet.id() == 'P')  {
//              printf("%c\n",packet.id());
//            }
            variable->value_ = entry.as<uint32_t>();
            variable->last_updated_millis_ = getMillis();
            break;
          }
        }
        variable = variable->next_;
      }
      break;
    }
    else variable = variable->next_id_;
  }
}



