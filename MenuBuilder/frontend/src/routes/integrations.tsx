import React, { useState } from "react";
import {
  Card,
  Row,
  Col,
  Typography,
  Tag,
  Button,
  Input,
  Select,
  Form,
  Space,
  message,
  Divider,
  Badge,
  Modal,
} from "antd";
import {
  ShopOutlined,
  QrcodeOutlined,
  IdcardOutlined,
  CustomerServiceOutlined,
  FileDoneOutlined,
  ApiOutlined,
  CheckCircleOutlined,
  ClockCircleOutlined,
  LockOutlined,
  ThunderboltOutlined,
  ArrowRightOutlined,
  SettingOutlined,
} from "@ant-design/icons";
import PageHeader from "../components/PageHeader";
import ApiConnectionPage from "./api-connection";

const { Title, Text, Paragraph } = Typography;

const CLOUD_KKT_PROVIDERS = [
  { value: "orangedata", label: "OrangeData" },
  { value: "atol", label: "Атол Онлайн" },
  { value: "evotor", label: "Эвотор Цифровая касса" },
  { value: "cloudkassir", label: "CloudKassir" },
  { value: "businessru", label: "Бизнес.Ру" },
];

export default function IntegrationsPage() {
  const [kktProvider, setKktProvider] = useState<string>("orangedata");
  const [kktApiKey, setKktApiKey] = useState<string>("");
  const [kktSaved, setKktSaved] = useState<boolean>(false);
  const [kktSaving, setKktSaving] = useState<boolean>(false);
  const [apiModalOpen, setApiModalOpen] = useState<boolean>(false);

  const handleSaveKkt = () => {
    if (!kktApiKey.trim()) {
      message.warning("Введите API-ключ облачной кассы");
      return;
    }
    setKktSaving(true);
    setTimeout(() => {
      setKktSaving(false);
      setKktSaved(true);
      message.success(
        `Настройки интеграции с ${
          CLOUD_KKT_PROVIDERS.find((p) => p.value === kktProvider)?.label
        } сохранены`
      );
    }, 400);
  };

  return (
    <div style={{ maxWidth: 1200, margin: "0 auto" }}>
      <PageHeader
        title="Витрина интеграций и сервисов"
        subtitle="Подключение облачных касс, СБП, СКУД, CRM, учетных систем и REST API"
      />

      {/* Grid of integration cards */}
      <Row gutter={[16, 16]}>
        {/* 1. Облачные кассы */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #d9d9d9",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#e6f4ff",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <ShopOutlined style={{ fontSize: 22, color: "#1677ff" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    Облачные кассы
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Интеграция с облачными кассами
                  </Text>
                </div>
              </Space>
              <Tag color={kktSaved ? "success" : "processing"}>
                {kktSaved ? "Подключено" : "Настройка"}
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Фискализация чеков в соответствии с 54-ФЗ через ведущие сервисы облачной кассы.
              Автоматическая отправка электронных чеков клиентам.
            </Paragraph>

            <div
              style={{
                background: "#fafafa",
                padding: 16,
                borderRadius: 6,
                marginTop: "auto",
              }}
            >
              <Form layout="vertical" size="small">
                <Form.Item label="Выберите поставщика:" style={{ marginBottom: 10 }}>
                  <Select
                    value={kktProvider}
                    onChange={(val) => {
                      setKktProvider(val);
                      setKktSaved(false);
                    }}
                    options={CLOUD_KKT_PROVIDERS}
                    style={{ width: "100%" }}
                  />
                </Form.Item>

                <Form.Item label="Введите АПИ-ключ:" style={{ marginBottom: 12 }}>
                  <Input.Password
                    prefix={<LockOutlined style={{ color: "#bfbfbf" }} />}
                    placeholder="Вставьте секретный API-ключ сервиса"
                    value={kktApiKey}
                    onChange={(e) => {
                      setKktApiKey(e.target.value);
                      setKktSaved(false);
                    }}
                  />
                </Form.Item>

                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                  <Button
                    type="primary"
                    loading={kktSaving}
                    onClick={handleSaveKkt}
                  >
                    {kktSaved ? "Обновить ключ" : "Сохранить интеграцию"}
                  </Button>
                  {kktSaved && (
                    <Text type="success" style={{ fontSize: 12 }}>
                      <CheckCircleOutlined /> Соединение активно
                    </Text>
                  )}
                </div>
              </Form>
            </div>
          </Card>
        </Col>

        {/* 2. СБП */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #d9d9d9",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#f6ffed",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <QrcodeOutlined style={{ fontSize: 22, color: "#52c41a" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    Система быстрых платежей (СБП)
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Безналичная оплата по динамическому QR
                  </Text>
                </div>
              </Space>
              <Tag color="warning" icon={<ClockCircleOutlined />}>
                Скоро
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Скоро сервис подключения к СБП (прием оплаты без банковского терминала).
            </Paragraph>

            <div
              style={{
                background: "#fcffe6",
                border: "1px dashed #d3f261",
                padding: 16,
                borderRadius: 6,
                marginTop: "auto",
              }}
            >
              <Text strong style={{ fontSize: 12, display: "block", marginBottom: 6 }}>
                Преимущества интеграции:
              </Text>
              <ul style={{ margin: 0, paddingLeft: 20, fontSize: 12, color: "#595959" }}>
                <li>Мгновенный прием платежей по QR-коду на экране терминала</li>
                <li>Сниженная комиссия по сравнению с классическим эквайрингом</li>
                <li>Работа без установки физического POS-терминала</li>
              </ul>
              <Button
                size="small"
                style={{ marginTop: 12 }}
                onClick={() => message.info("Вы подписаны на уведомление о запуске СБП")}
              >
                Уведомить о готовности
              </Button>
            </div>
          </Card>
        </Col>

        {/* 3. СКУД */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #d9d9d9",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#fff7e6",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <IdcardOutlined style={{ fontSize: 22, color: "#fa8c16" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    Интеграция со СКУД
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Системы контроля и управления доступом
                  </Text>
                </div>
              </Space>
              <Tag color="warning" icon={<ClockCircleOutlined />}>
                Скоро
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Скоро — сервис импорта списков из СКУД.
            </Paragraph>

            <div
              style={{
                background: "#fafafa",
                border: "1px dashed #d9d9d9",
                padding: 16,
                borderRadius: 6,
                marginTop: "auto",
              }}
            >
              <Text strong style={{ fontSize: 12, display: "block", marginBottom: 6 }}>
                Возможности модуля:
              </Text>
              <ul style={{ margin: 0, paddingLeft: 20, fontSize: 12, color: "#595959" }}>
                <li>Автоматическая синхронизация пропусков, RFID-карт и списков пользователей</li>
                <li>Быстрый поиск плательщиков по номеру карты или штрихкоду пропуска</li>
              </ul>
            </div>
          </Card>
        </Col>

        {/* 4. CRM */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #d9d9d9",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#f9f0ff",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <CustomerServiceOutlined style={{ fontSize: 22, color: "#722ed1" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    CRM-системы
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Управление клиентами и счетами
                  </Text>
                </div>
              </Space>
              <Tag color="warning" icon={<ClockCircleOutlined />}>
                Скоро
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Скоро:
            </Paragraph>

            <div
              style={{
                background: "#fafafa",
                border: "1px dashed #d9d9d9",
                padding: 16,
                borderRadius: 6,
                marginTop: "auto",
              }}
            >
              <ol style={{ margin: 0, paddingLeft: 20, fontSize: 12, color: "#595959" }}>
                <li style={{ marginBottom: 6 }}>
                  Сервис обмена клиентской БД с CRM-системами;
                </li>
                <li>
                  Загрузка счетов на оплату (оплата в терминалах по номеру счета или QR-коду).
                </li>
              </ol>
            </div>
          </Card>
        </Col>

        {/* 5. 1С, ERP */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #d9d9d9",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#fff0f6",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <FileDoneOutlined style={{ fontSize: 22, color: "#eb2f96" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    1С, ERP
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    Учетные и бухгалтерские системы
                  </Text>
                </div>
              </Space>
              <Tag color="warning" icon={<ClockCircleOutlined />}>
                Скоро
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Скоро — выгрузка реестров платежей.
            </Paragraph>

            <div
              style={{
                background: "#fafafa",
                border: "1px dashed #d9d9d9",
                padding: 16,
                borderRadius: 6,
                marginTop: "auto",
              }}
            >
              <Text strong style={{ fontSize: 12, display: "block", marginBottom: 6 }}>
                Функционал:
              </Text>
              <ul style={{ margin: 0, paddingLeft: 20, fontSize: 12, color: "#595959" }}>
                <li>Автоматическое формирование реестров по сменам и закрытию дня</li>
                <li>Прямой экспорт проводок и платежей в конфигурации 1С:Бухгалтерия и 1С:ERP</li>
              </ul>
            </div>
          </Card>
        </Col>

        {/* 6. Подключение по API (REST / X-API-Key) */}
        <Col xs={24} lg={12}>
          <Card
            bordered={false}
            style={{
              height: "100%",
              borderRadius: 8,
              border: "1px solid #adc6ff",
              background: "#f0f5ff",
              display: "flex",
              flexDirection: "column",
            }}
            styles={{
              body: {
                display: "flex",
                flexDirection: "column",
                flex: 1,
              },
            }}
          >
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 12 }}>
              <Space align="center" size={10}>
                <div
                  style={{
                    width: 40,
                    height: 40,
                    borderRadius: 8,
                    background: "#2f54eb",
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                  }}
                >
                  <ApiOutlined style={{ fontSize: 22, color: "#ffffff" }} />
                </div>
                <div>
                  <Title level={5} style={{ margin: 0 }}>
                    Подключение по API
                  </Title>
                  <Text type="secondary" style={{ fontSize: 13 }}>
                    REST API, X-API-Key и интеграционная документация
                  </Text>
                </div>
              </Space>
              <Tag color="geekblue">
                Активно
              </Tag>
            </div>

            <Paragraph style={{ color: "#595959", fontSize: 13, marginBottom: 16 }}>
              Управление API-ключом организации для доступа к REST API, отправки RPC-команд,
              чтения телеметрии и документации Swagger.
            </Paragraph>

            <div style={{ marginTop: "auto" }}>
              <Button
                type="primary"
                icon={<SettingOutlined />}
                onClick={() => setApiModalOpen(true)}
              >
                Открыть параметры API и документацию
              </Button>
            </div>
          </Card>
        </Col>
      </Row>

      {/* Modal for API Connection management & Docs */}
      <Modal
        title="Параметры подключения по API"
        open={apiModalOpen}
        onCancel={() => setApiModalOpen(false)}
        footer={null}
        width={960}
        destroyOnClose
      >
        <ApiConnectionPage />
      </Modal>
    </div>
  );
}
