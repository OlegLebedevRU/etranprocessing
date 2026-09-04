import { useState } from "react";
import {
  Alert,
  Badge,
  Button,
  Card,
  Col,
  Divider,
  Input,
  Row,
  Space,
  Table,
  Tabs,
  Tag,
  Timeline,
  Typography,
} from "antd";
import {
  AppstoreOutlined,
  BookOutlined,
  CheckCircleOutlined,
  CopyOutlined,
  DeleteOutlined,
  EditOutlined,
  ExclamationCircleOutlined,
  FolderAddOutlined,
  FolderOutlined,
  InfoCircleOutlined,
  LinkOutlined,
  MobileOutlined,
  PlusCircleOutlined,
  PlusOutlined,
  PrinterOutlined,
  QuestionCircleOutlined,
  SyncOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import PageHeader from "../components/PageHeader";

const { Title, Text, Paragraph } = Typography;

export default function MenuHelpPage() {
  const [activeTab, setActiveTab] = useState("overview");

  return (
    <div style={{ maxWidth: 1040, margin: "0 auto", padding: "0 8px 32px" }}>
      <PageHeader
        title="Инструкция пользователя: Управление меню"
        subtitle="Наглядное руководство по изменению, добавлению сущностей и логике работы системы"
      />

      <Tabs
        activeKey={activeTab}
        onChange={setActiveTab}
        type="card"
        style={{ marginTop: 8 }}
        items={[
          {
            key: "overview",
            label: (
              <span>
                <InfoCircleOutlined /> Обзор и концепция флоу
              </span>
            ),
            children: <OverviewSection onNavigate={setActiveTab} />,
          },
          {
            key: "catalog",
            label: (
              <span>
                <FolderOutlined /> 1. Каталог (Справочник)
              </span>
            ),
            children: <CatalogHelpSection />,
          },
          {
            key: "variants",
            label: (
              <span>
                <AppstoreOutlined /> 2. Меню (Конструктор экранов)
              </span>
            ),
            children: <VariantsHelpSection />,
          },
          {
            key: "terminals",
            label: (
              <span>
                <MobileOutlined /> 3. Терминалы (Привязка)
              </span>
            ),
            children: <TerminalsHelpSection />,
          },
          {
            key: "faq",
            label: (
              <span>
                <QuestionCircleOutlined /> Частые вопросы и ошибки
              </span>
            ),
            children: <FaqSection />,
          },
        ]}
      />
    </div>
  );
}

/* =========================================================================
   1. ОБЗОР И КОНЦЕПЦИЯ ФЛОУ
   ========================================================================= */
function OverviewSection({ onNavigate }: { onNavigate: (tab: string) => void }) {
  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <Alert
        message="Ключевое правило безопасности системы"
        description="Любые изменения в каталоге или конструкторе меню сначала сохраняются на сервере и не ломают текущую работу терминалов. Терминалы обновляют свою конфигурацию планово только тогда, когда вы подтверждаете выпуск новой версии."
        type="info"
        showIcon
      />

      <Card title="Архитектурная цепочка: Как связаны сущности">
        <Row gutter={[16, 16]} align="middle">
          <Col xs={24} md={7}>
            <div
              style={{
                background: "#f0f5ff",
                border: "2px solid #adc6ff",
                borderRadius: 8,
                padding: 16,
                textAlign: "center",
              }}
            >
              <FolderOutlined style={{ fontSize: 32, color: "#1d39c4", marginBottom: 8 }} />
              <Title level={5} style={{ margin: "0 0 6px", color: "#1d39c4" }}>
                1. Каталог
              </Title>
              <Text type="secondary" style={{ fontSize: 12, display: "block" }}>
                «Центральный склад»
              </Text>
              <Paragraph style={{ fontSize: 13, marginTop: 8, marginBottom: 12 }}>
                Единый эталонный перечень услуг организации, категорий и фиксированных цен.
              </Paragraph>
              <Button size="small" type="primary" ghost onClick={() => onNavigate("catalog")}>
                Подробнее о Каталоге
              </Button>
            </div>
          </Col>

          <Col xs={24} md={1} style={{ textAlign: "center" }}>
            <span style={{ fontSize: 24, color: "#8c8c8c" }}>➔</span>
          </Col>

          <Col xs={24} md={8}>
            <div
              style={{
                background: "#f6ffed",
                border: "2px solid #b7eb8f",
                borderRadius: 8,
                padding: 16,
                textAlign: "center",
              }}
            >
              <AppstoreOutlined style={{ fontSize: 32, color: "#389e0d", marginBottom: 8 }} />
              <Title level={5} style={{ margin: "0 0 6px", color: "#389e0d" }}>
                2. Варианты Меню
              </Title>
              <Text type="secondary" style={{ fontSize: 12, display: "block" }}>
                «Витрина кнопок»
              </Text>
              <Paragraph style={{ fontSize: 13, marginTop: 8, marginBottom: 12 }}>
                Набор экранов (групп) и кнопок. Набирается из услуг Каталога под конкретный тип объекта.
              </Paragraph>
              <Button size="small" type="primary" ghost onClick={() => onNavigate("variants")}>
                Подробнее о Меню
              </Button>
            </div>
          </Col>

          <Col xs={24} md={1} style={{ textAlign: "center" }}>
            <span style={{ fontSize: 24, color: "#8c8c8c" }}>➔</span>
          </Col>

          <Col xs={24} md={7}>
            <div
              style={{
                background: "#fff7e6",
                border: "2px solid #ffd591",
                borderRadius: 8,
                padding: 16,
                textAlign: "center",
              }}
            >
              <MobileOutlined style={{ fontSize: 32, color: "#d46b08", marginBottom: 8 }} />
              <Title level={5} style={{ margin: "0 0 6px", color: "#d46b08" }}>
                3. Терминалы
              </Title>
              <Text type="secondary" style={{ fontSize: 12, display: "block" }}>
                «Аппараты самообслуживания»
              </Text>
              <Paragraph style={{ fontSize: 13, marginTop: 8, marginBottom: 12 }}>
                Физические устройства. Вы просто указываете, какой вариант меню должен загружать терминал.
              </Paragraph>
              <Button size="small" type="primary" ghost onClick={() => onNavigate("terminals")}>
                Подробнее о Терминалах
              </Button>
            </div>
          </Col>
        </Row>
      </Card>

      <Card title="Базовый порядок работы: 3 простых шага">
        <Timeline
          items={[
            {
              color: "blue",
              children: (
                <div>
                  <Text strong>Шаг 1: Добавить или изменить услугу в Каталоге</Text>
                  <Paragraph type="secondary" style={{ margin: 0, fontSize: 13 }}>
                    Если у вас появилась новая услуга или изменилась цена, вы вносите правку в Каталоге и нажимаете кнопку <strong>«Обновить изменения каталога в связанных меню»</strong>.
                  </Paragraph>
                </div>
              ),
            },
            {
              color: "green",
              children: (
                <div>
                  <Text strong>Шаг 2: Настроить кнопки в Варианте меню</Text>
                  <Paragraph type="secondary" style={{ margin: 0, fontSize: 13 }}>
                    В разделе <strong>Меню</strong> вы формируете структуру папок (групп) и добавляете нужные услуги на экраны, контролируя внешний вид кнопки во встроенном предпросмотре.
                  </Paragraph>
                </div>
              ),
            },
            {
              color: "orange",
              children: (
                <div>
                  <Text strong>Шаг 3: Назначить меню на нужные терминалы</Text>
                  <Paragraph type="secondary" style={{ margin: 0, fontSize: 13 }}>
                    В разделе <strong>Терминалы</strong> вы привязываете подготовленный вариант меню к конкретным аппаратам.
                  </Paragraph>
                </div>
              ),
            },
          ]}
        />
      </Card>
    </Space>
  );
}

/* =========================================================================
   2. СПРАВОЧНИК КАТАЛОГ
   ========================================================================= */
function CatalogHelpSection() {
  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <Card
        title={
          <Space>
            <FolderOutlined style={{ color: "#1890ff" }} />
            <span>Разделы каталога: Добавление и изменение категорий</span>
          </Space>
        }
      >
        <Paragraph>
          Разделы каталога позволяют структурировать перечень услуг по направлениям деятельности (например: <em>«Мойка»</em>, <em>«Шиномонтаж»</em>, <em>«Кафе»</em>). Система поддерживает древовидную вложенность до <strong>3 уровней</strong>.
        </Paragraph>

        <Row gutter={[16, 16]}>
          <Col xs={24} md={12}>
            <div
              style={{
                border: "2px dashed #1890ff",
                borderRadius: 8,
                padding: 14,
                background: "#fafafa",
              }}
            >
              <Text strong style={{ display: "block", marginBottom: 8, color: "#1890ff" }}>
                Скриншот-макет: Дерево каталога слева
              </Text>
              <div style={{ background: "#fff", padding: 12, borderRadius: 6, border: "1px solid #d9d9d9" }}>
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 10 }}>
                  <Text strong>Разделы каталога</Text>
                  <Tag color="blue" style={{ cursor: "pointer" }}>
                    <PlusOutlined /> Раздел
                  </Tag>
                </div>
                <div style={{ padding: "4px 8px", background: "#e6f7ff", borderRadius: 4, marginBottom: 6 }}>
                  <FolderOutlined /> Все разделы каталога
                </div>
                <div style={{ paddingLeft: 16 }}>
                  <div style={{ display: "flex", justifyContent: "space-between", padding: "3px 0" }}>
                    <span>📁 1. Моечные услуги</span>
                    <Space size={4}>
                      <Tag color="geekblue" style={{ fontSize: 10 }}>+ Подраздел</Tag>
                      <EditOutlined style={{ fontSize: 12, color: "#1890ff" }} />
                    </Space>
                  </div>
                  <div style={{ paddingLeft: 16, color: "#595959", fontSize: 12 }}>
                    <div>└ 📁 Бесконтактная мойка</div>
                    <div>└ 📁 Сушка и полировка</div>
                  </div>
                </div>
              </div>
            </div>
          </Col>

          <Col xs={24} md={12}>
            <Title level={5}>Пошаговые действия:</Title>
            <ol style={{ paddingLeft: 18, lineHeight: 1.8 }}>
              <li>
                <strong>Создать корневой раздел:</strong> нажмите синюю кнопку <code>+ Раздел</code> в шапке левой панели.
              </li>
              <li>
                <strong>Создать вложенный подраздел:</strong> наведите курсор на родительскую категорию и нажмите появившуюся кнопку <code>+ Подраздел</code>.
              </li>
              <li>
                <strong>Редактировать:</strong> нажмите иконку карандаша <code><EditOutlined /></code> рядом с названием раздела.
              </li>
              <li>
                <strong>Удалить:</strong> нажмите иконку корзины <code><DeleteOutlined /></code> (удаление возможно, только если в разделе нет услуг).
              </li>
            </ol>
          </Col>
        </Row>
      </Card>

      <Card
        title={
          <Space>
            <BookOutlined style={{ color: "#1890ff" }} />
            <span>Услуги каталога: Создание и параметры полей</span>
          </Space>
        }
      >
        <Paragraph>
          Чтобы добавить новую позицию в каталог, нажмите кнопку <strong>«+ Добавить услугу»</strong> над правой таблицей.
        </Paragraph>

        <Table
          size="small"
          pagination={false}
          bordered
          columns={[
            { title: "Поле формы", dataIndex: "field", key: "field", width: 200, render: (t) => <strong>{t}</strong> },
            { title: "Обязательно?", dataIndex: "req", key: "req", width: 120, render: (r) => (r ? <Tag color="red">Да</Tag> : <Tag>Нет</Tag>) },
            { title: "Назначение и рекомендации по заполнению", dataIndex: "desc", key: "desc" },
          ]}
          dataSource={[
            {
              key: "1",
              field: "Раздел каталога",
              req: true,
              desc: "Папка, в которую будет помещена услуга для удобства поиска и фильтрации.",
            },
            {
              key: "2",
              field: "Код ТСП",
              req: true,
              desc: "Уникальный цифровой номер услуги в системе. Для каталога автоматически предлагаются свободные номера из диапазона 1000301..1000999.",
            },
            {
              key: "3",
              field: "Название услуги",
              req: true,
              desc: "Текст, отображаемый в системе и на экранной кнопке терминала.",
            },
            {
              key: "4",
              field: "Наименование для чека",
              req: false,
              desc: "Текст, который печатается на кассовом фискальном чеке. Если оставить пустым, в чек пойдет обычное название.",
            },
            {
              key: "5",
              field: "Цена (₽)",
              req: true,
              desc: "Стоимость в рублях. Укажите 0, если услуга бесплатная или сумма вводится клиентом вручную.",
            },
            {
              key: "6",
              field: "Номер Прототипа",
              req: false,
              desc: "Служебный номер сценария оплаты (по умолчанию 0).",
            },
          ]}
        />
      </Card>

      <Alert
        message="ГЛАВНОЕ ДЕЙСТВИЕ: Кнопка «Обновить изменения каталога в связанных меню»"
        description={
          <div>
            <Paragraph style={{ margin: "6px 0" }}>
              Когда вы меняете цену или название существующей услуги в каталоге, эти изменения сохраняются в эталонной базе. Чтобы эти новые цены <strong>применились во всех меню терминалов</strong>, необходимо нажать кнопку:
            </Paragraph>
            <div style={{ margin: "12px 0" }}>
              <Button type="primary" icon={<SyncOutlined />} style={{ background: "#fa8c16", borderColor: "#fa8c16" }}>
                Обновить изменения каталога в связанных меню
              </Button>
            </div>
            <Paragraph style={{ margin: "6px 0", fontSize: 13 }}>
              Система автоматически найдет все варианты меню, где используется обновленная услуга, обновит там значения и <strong>выпустит новую версию меню</strong> (например, была v1, станет v2).
            </Paragraph>
          </div>
        }
        type="warning"
        showIcon
      />
    </Space>
  );
}

/* =========================================================================
   3. КОНСТРУКТОР МЕНЮ
   ========================================================================= */
function VariantsHelpSection() {
  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <Card title="Трехпанельный интерфейс конструктора меню">
        <Paragraph>
          Страница <strong>Меню</strong> устроена по принципу последовательного выбора слева направо:
        </Paragraph>
        <Row gutter={[12, 12]}>
          <Col xs={24} md={8}>
            <div style={{ border: "2px solid #91caff", background: "#e6f4ff", padding: 12, borderRadius: 6 }}>
              <Text strong style={{ color: "#0958d9" }}>1. Колонка «Варианты меню»</Text>
              <Paragraph style={{ fontSize: 12, margin: "6px 0 0" }}>
                Список вариантов конфигураций (например: «Основное меню мойки», «Ночной тариф»). Здесь выбирается активный вариант для настройки.
              </Paragraph>
            </div>
          </Col>
          <Col xs={24} md={8}>
            <div style={{ border: "2px solid #b7eb8f", background: "#f6ffed", padding: 12, borderRadius: 6 }}>
              <Text strong style={{ color: "#389e0d" }}>2. Колонка «Структура групп»</Text>
              <Paragraph style={{ fontSize: 12, margin: "6px 0 0" }}>
                Папки и экраны внутри выбранного меню. Выберите нужную группу, чтобы увидеть ее кнопки-услуги.
              </Paragraph>
            </div>
          </Col>
          <Col xs={24} md={8}>
            <div style={{ border: "2px solid #ffd591", background: "#fffbe6", padding: 12, borderRadius: 6 }}>
              <Text strong style={{ color: "#d46b08" }}>3. Колонка «Услуги группы»</Text>
              <Paragraph style={{ fontSize: 12, margin: "6px 0 0" }}>
                Таблица экранных кнопок в выбранной группе. Добавление, редактирование цен, предпросмотр кнопки.
              </Paragraph>
            </div>
          </Col>
        </Row>
      </Card>

      <Card title="Работа с Вариантами меню (Левая колонка)">
        <Row gutter={[16, 16]}>
          <Col xs={24} md={12}>
            <div style={{ border: "1px solid #d9d9d9", borderRadius: 6, padding: 12, background: "#fafafa" }}>
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 8 }}>
                <Text strong>Варианты меню</Text>
                <Space size={4}>
                  <Tag color="blue" title="Создать"><PlusOutlined /> Новый</Tag>
                  <Tag color="cyan" title="Копировать"><CopyOutlined /> Копия</Tag>
                  <Tag color="red" title="Удалить"><DeleteOutlined /></Tag>
                </Space>
              </div>
              <div style={{ background: "#e6f4ff", border: "1px solid #91caff", borderRadius: 4, padding: "6px 10px", display: "flex", justifyContent: "space-between" }}>
                <span><strong>Стандартное меню ТСП</strong></span>
                <Tag color="geekblue">v3</Tag>
              </div>
            </div>
          </Col>

          <Col xs={24} md={12}>
            <ul style={{ paddingLeft: 18, lineHeight: 1.8 }}>
              <li>
                <strong>Создать новый вариант меню:</strong> нажмите кнопку <code>+</code> в шапке карточки. Введите название. Будет создан пустой шаблон меню.
              </li>
              <li>
                <strong>Копировать существующий вариант (Рекомендуется):</strong> выделите существующее меню и нажмите иконку <code><CopyOutlined /></code>. Будет создана полная копия всех групп и услуг. <em>Это самый быстрый и безопасный способ внести сезонные изменения, не трогая рабочее меню.</em>
              </li>
              <li>
                <strong>Удалить меню:</strong> нажмите иконку <code><DeleteOutlined /></code>. Если вариант привязан к терминалу, система предупредит об этом.
              </li>
            </ul>
          </Col>
        </Row>
      </Card>

      <Card title="Работа с Группами (Папками экранов)">
        <Paragraph>
          Группы соответствуют разделам на сенсорном экране терминала. При нажатии на кнопку группы терминал открывает список услуг или подгрупп внутри нее.
        </Paragraph>
        <Row gutter={[16, 16]}>
          <Col xs={24} md={12}>
            <div style={{ background: "#f8fafc", border: "1px solid #cbd5e1", borderRadius: 6, padding: 12 }}>
              <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 8 }}>
                <Text strong>Структура групп</Text>
                <Button type="link" size="small" icon={<FolderAddOutlined />}>+ Группа</Button>
              </div>
              <div style={{ paddingLeft: 8, fontSize: 13 }}>
                <div>📁 <strong>801.</strong> Основные программы</div>
                <div style={{ paddingLeft: 16 }}>
                  <div>📁 <strong>802.</strong> Экспресс-мойка</div>
                  <div>📁 <strong>803.</strong> Комплекс Люкс</div>
                </div>
                <div>📁 <strong>804.</strong> Дополнительные услуги</div>
              </div>
              <Divider style={{ margin: "8px 0" }} />
              <Space wrap size="small">
                <Button size="small" icon={<PlusOutlined />}>+ Подгруппа</Button>
                <Button size="small" icon={<EditOutlined />}>Изменить</Button>
                <Button size="small" danger icon={<DeleteOutlined />}>Удалить</Button>
              </Space>
            </div>
          </Col>

          <Col xs={24} md={12}>
            <ul style={{ paddingLeft: 18, lineHeight: 1.8 }}>
              <li>
                <strong>Номер группы:</strong> задается числом от <code>801</code> до <code>999</code>. Он определяет порядок расположения кнопок на экране.
              </li>
              <li>
                <strong>Изменение структуры (Drag-and-Drop):</strong> вы можете зажать группу мышью и перетащить ее выше, ниже или внутрь другой группы.
              </li>
              <li>
                <strong style={{ color: "#cf1322" }}>Внимание при удалении:</strong> удаление группы удаляет все находящиеся внутри подгруппы и кнопки услуг!
              </li>
            </ul>
          </Col>
        </Row>
      </Card>

      <Card title="Добавление услуги в группу: 2 режима и Live Preview">
        <Paragraph>
          Чтобы добавить кнопку на экран, выберите группу в центре и нажмите синюю кнопку <strong>«+ Добавить услугу»</strong> в правой панели.
        </Paragraph>

        <Row gutter={[24, 24]}>
          <Col xs={24} md={14}>
            <Title level={5}>Два режима создания:</Title>
            <div style={{ marginBottom: 14 }}>
              <Tag color="blue" style={{ fontSize: 13, padding: "4px 8px" }}>
                <BookOutlined /> Режим «Выбрать из каталога» (Основной)
              </Tag>
              <Paragraph style={{ margin: "6px 0", fontSize: 13 }}>
                Вы просто выбираете услугу из выпадающего списка. Все реквизиты (название, цена, ТСП, чек) подставляются автоматически. При изменении цены в каталоге эта кнопка обновится централизованно.
              </Paragraph>
            </div>

            <div>
              <Tag color="cyan" style={{ fontSize: 13, padding: "4px 8px" }}>
                <PlusCircleOutlined /> Режим «Создать новую услугу»
              </Tag>
              <Paragraph style={{ margin: "6px 0", fontSize: 13 }}>
                Используется для разовых или уникальных услуг. Вы выбираете свободный локальный код ТСП (диапазон <code>1001301..1001999</code>). При необходимости можно включить галочку <em>«Добавить услугу в каталог»</em>, чтобы она стала доступна и в других меню.
              </Paragraph>
            </div>
          </Col>

          <Col xs={24} md={10}>
            <div
              style={{
                background: "#f8fafc",
                border: "2px solid #cbd5e1",
                borderRadius: 10,
                padding: 16,
                textAlign: "center",
              }}
            >
              <Text strong style={{ display: "block", marginBottom: 12, color: "#334155" }}>
                <MobileOutlined /> Встроенный предпросмотр кнопки (Live Preview)
              </Text>

              {/* Макет кнопки терминала */}
              <div
                style={{
                  width: 220,
                  minHeight: 110,
                  margin: "0 auto",
                  background: "#1e3a5f",
                  borderRadius: 12,
                  boxShadow: "0 4px 12px rgba(30, 58, 95, 0.25)",
                  border: "2px solid #2d5a88",
                  padding: "10px 12px",
                  display: "flex",
                  flexDirection: "column",
                  justifyContent: "space-between",
                  color: "#ffffff",
                  textAlign: "left",
                }}
              >
                <div style={{ fontSize: 13, fontWeight: 600, lineHeight: 1.3, color: "#f1f5f9" }}>
                  2. Стрижка со сменой насадок
                </div>
                <div style={{ display: "flex", justifyContent: "flex-end", marginTop: 8, paddingTop: 4, borderTop: "1px solid rgba(255,255,255,0.12)" }}>
                  <span style={{ background: "rgba(255,255,255,0.18)", borderRadius: 4, padding: "1px 7px", fontSize: 12, fontWeight: 700, color: "#93c5fd" }}>
                    450 ₽
                  </span>
                </div>
              </div>

              {/* Макет чека */}
              <div
                style={{
                  width: 220,
                  margin: "12px auto 0",
                  background: "#fff",
                  border: "1px dashed #cbd5e1",
                  borderRadius: 6,
                  padding: "6px 8px",
                  fontSize: 11,
                  textAlign: "left",
                  color: "#475569",
                }}
              >
                <div style={{ fontSize: 10, fontWeight: 600, color: "#94a3b8" }}>
                  <PrinterOutlined /> В чеке ККТ:
                </div>
                <div style={{ fontFamily: "monospace", color: "#1e293b", marginTop: 2 }}>
                  Стрижка (насадки)
                </div>
              </div>

              <Text type="secondary" style={{ fontSize: 11, display: "block", marginTop: 8 }}>
                Во время ввода названия вы сразу видите переносы строк (до 4 строк) и чек.
              </Text>
            </div>
          </Col>
        </Row>
      </Card>
    </Space>
  );
}

/* =========================================================================
   4. ТЕРМИНАЛЫ И ПРИВЯЗКА
   ========================================================================= */
function TerminalsHelpSection() {
  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <Card title="Привязка меню к терминалам: Назначение конфигурации">
        <Paragraph>
          В разделе <strong>Терминалы</strong> оператор связывает физический аппарат с подготовленным вариантом меню.
        </Paragraph>

        <Row gutter={[16, 16]}>
          <Col xs={24} md={14}>
            <div style={{ border: "1px solid #e2e8f0", borderRadius: 8, padding: 12, background: "#fff" }}>
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 10 }}>
                <Text strong>Список терминалов</Text>
                <Input size="small" placeholder="Поиск по ID / адресу..." style={{ width: 180 }} />
              </div>
              <table style={{ width: "100%", fontSize: 12, borderCollapse: "collapse" }}>
                <thead>
                  <tr style={{ background: "#f8fafc", borderBottom: "1px solid #e2e8f0" }}>
                    <th style={{ padding: "6px 8px", textAlign: "left" }}>ID</th>
                    <th style={{ padding: "6px 8px", textAlign: "left" }}>Меню</th>
                    <th style={{ padding: "6px 8px", textAlign: "left" }}>Версия</th>
                    <th style={{ padding: "6px 8px", textAlign: "center" }}>Действия</th>
                  </tr>
                </thead>
                <tbody>
                  <tr style={{ borderBottom: "1px solid #f1f5f9" }}>
                    <td style={{ padding: "6px 8px" }}><strong>#101</strong></td>
                    <td style={{ padding: "6px 8px" }}><Tag color="blue">Основная мойка</Tag></td>
                    <td style={{ padding: "6px 8px" }}><Tag color="success">v3 (актуальная)</Tag></td>
                    <td style={{ padding: "6px 8px", textAlign: "center" }}><Button size="small" type="primary" ghost>Изменить</Button></td>
                  </tr>
                  <tr style={{ borderBottom: "1px solid #f1f5f9" }}>
                    <td style={{ padding: "6px 8px" }}><strong>#102</strong></td>
                    <td style={{ padding: "6px 8px" }}><Tag color="blue">Ночной тариф</Tag></td>
                    <td style={{ padding: "6px 8px" }}><Tag color="warning">v2 (на сервере v3)</Tag></td>
                    <td style={{ padding: "6px 8px", textAlign: "center" }}><Button size="small" type="primary" ghost>Изменить</Button></td>
                  </tr>
                </tbody>
              </table>
            </div>
          </Col>

          <Col xs={24} md={10}>
            <Title level={5}>Как привязать или сменить меню:</Title>
            <ol style={{ paddingLeft: 18, lineHeight: 1.8 }}>
              <li>Найдите терминал в списке по номеру ID или адресу через поиск.</li>
              <li>Нажмите кнопку <code>Привязать</code> (или <code>Изменить</code>).</li>
              <li>В выпадающем списке выберите нужный вариант меню и нажмите <code>Сохранить</code>.</li>
            </ol>
          </Col>
        </Row>
      </Card>

      <Card title="Светофор версий меню: Что означают метки в таблице">
        <Space direction="vertical" size="middle" style={{ width: "100%" }}>
          <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
            <Tag color="success" style={{ minWidth: 150, textAlign: "center", padding: "3px 8px", fontSize: 12 }}>
              v3 (актуальная)
            </Tag>
            <div>
              <Text strong style={{ color: "#389e0d" }}>Все в порядке</Text>
              <Paragraph style={{ margin: 0, fontSize: 13, color: "#595959" }}>
                Терминал уже загрузил последнюю созданную на сервере версию меню. Никаких дополнительных действий не требуется.
              </Paragraph>
            </div>
          </div>

          <Divider style={{ margin: "4px 0" }} />

          <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
            <Tag color="warning" style={{ minWidth: 150, textAlign: "center", padding: "3px 8px", fontSize: 12 }}>
              v2 (на сервере v3)
            </Tag>
            <div>
              <Text strong style={{ color: "#d46b08" }}>Меню отредактировано на сервере</Text>
              <Paragraph style={{ margin: 0, fontSize: 13, color: "#595959" }}>
                Вы внесли правки в меню (или обновили каталог), и сервер выпустил новую версию <code>v3</code>. На терминале пока работает прежняя версия <code>v2</code>. Терминал применит новое меню при очередном сеансе связи или регламентной перезагрузке.
              </Paragraph>
            </div>
          </div>

          <Divider style={{ margin: "4px 0" }} />

          <div style={{ display: "flex", gap: 12, alignItems: "flex-start" }}>
            <Tag color="default" style={{ minWidth: 150, textAlign: "center", padding: "3px 8px", fontSize: 12 }}>
              Не загружена
            </Tag>
            <div>
              <Text strong style={{ color: "#595959" }}>Новая привязка</Text>
              <Paragraph style={{ margin: 0, fontSize: 13, color: "#595959" }}>
                Меню только что назначено на терминал. Терминал скачает его при первом обращении к серверу.
              </Paragraph>
            </div>
          </div>
        </Space>
      </Card>
    </Space>
  );
}

/* =========================================================================
   5. ЧАСТЫЕ ВОПРОСЫ И ОШИБКИ (FAQ)
   ========================================================================= */
function FaqSection() {
  return (
    <Space direction="vertical" size="large" style={{ width: "100%" }}>
      <Card title="Справочник диапазонов номеров (ТСП и группы)">
        <Table
          size="small"
          pagination={false}
          bordered
          columns={[
            { title: "Сущность", dataIndex: "entity", key: "entity", width: 220, render: (t) => <strong>{t}</strong> },
            { title: "Диапазон кодов", dataIndex: "range", key: "range", width: 180, render: (r) => <Tag color="blue">{r}</Tag> },
            { title: "Где используется и зачем", dataIndex: "desc", key: "desc" },
          ]}
          dataSource={[
            {
              key: "1",
              entity: "Каталожные услуги",
              range: "1000301 .. 1000999",
              desc: "Общие системные услуги каталога. Доступны для добавления во все меню всех терминалов организации.",
            },
            {
              key: "2",
              entity: "Локальные услуги меню",
              range: "1001301 .. 1001999",
              desc: "Уникальные кнопки конкретного варианта меню, не входящие в общий справочник каталога.",
            },
            {
              key: "3",
              entity: "Номера групп (экранов)",
              range: "801 .. 999",
              desc: "Порядковый номер группы кнопок. Определяет очередность папок на мониторе терминала.",
            },
          ]}
        />
      </Card>

      <Card title="Частые ошибки операторов и как их избежать">
        <Space direction="vertical" size="middle" style={{ width: "100%" }}>
          <Alert
            message="1. Изменили цену в Каталоге, но на терминале отображается старая цена"
            description={
              <div>
                <Text type="secondary">
                  <strong>Причина:</strong> Не была нажата кнопка применения изменений.
                </Text>
                <div style={{ marginTop: 4 }}>
                  <strong>Решение:</strong> Перейдите во вкладку <strong>Каталог</strong> и нажмите оранжевую кнопку <code>«Обновить изменения каталога в связанных меню»</code>. Меню получат новые версии, и терминалы обновят цены.
                </div>
              </div>
            }
            type="error"
            showIcon
          />

          <Alert
            message="2. Текст на кнопке терминала не влезает или обрезается троеточием"
            description={
              <div>
                <Text type="secondary">
                  <strong>Причина:</strong> Слишком длинное слово без пробелов или текст длиннее 4 строк.
                </Text>
                <div style={{ marginTop: 4 }}>
                  <strong>Решение:</strong> При редактировании услуги всегда смотрите в блок <strong>«Предпросмотр на терминале»</strong> справа. Используйте короткие общепринятые сокращения.
                </div>
              </div>
            }
            type="warning"
            showIcon
          />

          <Alert
            message="3. Как безопасно обновить меню без риска для работающих терминалов?"
            description={
              <div>
                <Text type="secondary">
                  <strong>Рекомендованный регламент:</strong>
                </Text>
                <ol style={{ margin: "4px 0 0", paddingLeft: 20 }}>
                  <li>В разделе <strong>Меню</strong> найдите текущее меню и нажмите <code>Копировать</code>.</li>
                  <li>В копии спокойно добавьте новые услуги, проверьте предпросмотр и цены.</li>
                  <li>В разделе <strong>Терминалы</strong> переключите терминал на новую копию меню.</li>
                  <li>Если что-то пойдет не так, вы всегда сможете мгновенно переключить терминал обратно на старое меню в один клик.</li>
                </ol>
              </div>
            }
            type="info"
            showIcon
          />
        </Space>
      </Card>
    </Space>
  );
}
